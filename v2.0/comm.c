#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

#include "comm.h"

#define NUM_PLAYERS 4
#define SERVER_PORT "8292"
#define MAX_PAYLOAD 4096U

/* Wire protocol message types. */
enum message_type
{
    MSG_REGISTER = 1,
    MSG_REGISTER_OK,
    MSG_REGISTER_ERROR,
    MSG_MESSAGE,
    MSG_ACTION_REQUEST,
    MSG_INPUT_MODE,
    MSG_ACTION,
    MSG_CHAT
};

typedef struct
{
    uint32_t type;
    uint32_t length;
} wire_header;

typedef struct
{
    unsigned char header[sizeof(wire_header)];
    size_t header_used;
    uint32_t type;
    uint32_t length;
    unsigned char payload[MAX_PAYLOAD + 1U];
    size_t payload_used;
} recv_state;

static int is_server = 0;
static int listen_fd = -1;
static int server_fd = -1;
static int player_fds[NUM_PLAYERS] = {-1, -1, -1, -1};
static int pending_fds[NUM_PLAYERS] = {-1, -1, -1, -1};
static recv_state player_rx[NUM_PLAYERS];
static recv_state pending_rx[NUM_PLAYERS];
static recv_state server_rx;

static int timer_fd = -1;
static timer_handler active_timer_cb = NULL;

static int local_player_id = -1;
static comm_input_mode local_input_mode = COMM_INPUT_NONE;

static message_handler player_message_cb = NULL;
static action_handler player_action_cb = NULL;
static input_mode_handler player_mode_cb = NULL;

static connect_handler server_connect_cb = NULL;
static server_action_handler server_action_cb = NULL;
static server_chat_handler server_chat_cb = NULL;

static int stdin_eof = 0;
static char *completed_line = NULL;

static void reset_rx(recv_state *rx)
{
    memset(rx, 0, sizeof(*rx));
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -errno;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -errno;
    return 0;
}

static int send_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *p = buffer;
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t n = send(fd, p + sent, length - sent, MSG_NOSIGNAL);
        if (n > 0)
        {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
            int r;
            do
            {
                r = poll(&pfd, 1, -1);
            } while (r < 0 && errno == EINTR);
            if (r <= 0)
                return (r < 0) ? -errno : -EIO;
            continue;
        }
        return (n == 0) ? -EPIPE : -errno;
    }

    return 0;
}

static int send_packet(int fd, uint32_t type, const void *payload, uint32_t length)
{
    if (fd < 0 || length > MAX_PAYLOAD)
        return -EINVAL;

    wire_header header;
    header.type = htonl(type);
    header.length = htonl(length);

    int r = send_all(fd, &header, sizeof(header));
    if (r < 0)
        return r;

    if (length > 0)
        return send_all(fd, payload, length);

    return 0;
}

static int send_string_packet(int fd, uint32_t type, const char *text)
{
    if (text == NULL)
        return -EINVAL;

    size_t length = strlen(text);
    if (length > MAX_PAYLOAD)
        return -EMSGSIZE;

    return send_packet(fd, type, text, (uint32_t)length);
}

static void readline_line_ready(char *line)
{
    if (line == NULL)
    {
        stdin_eof = 1;
        return;
    }

    printf("\033[1A\r\033[2K");
    fflush(stdout);

    free(completed_line);
    completed_line = line;
}

static void change_local_mode(comm_input_mode mode)
{
    local_input_mode = mode;
    if (player_mode_cb != NULL)
        player_mode_cb(mode);
}

static int dispatch_player_packet(uint32_t type, const unsigned char *payload, uint32_t length)
{
    char text[MAX_PAYLOAD + 1U];

    if (length > MAX_PAYLOAD)
        return -EMSGSIZE;

    memcpy(text, payload, length);
    text[length] = '\0';

    switch (type)
    {
    case MSG_MESSAGE:
        if (player_message_cb != NULL)
            player_message_cb(text);
        return 0;

    case MSG_ACTION_REQUEST:
        change_local_mode(COMM_INPUT_ACTION);
        if (player_action_cb != NULL)
            player_action_cb(text);
        return 0;

    case MSG_INPUT_MODE:
        if (length != sizeof(uint32_t))
            return -EPROTO;
        {
            uint32_t value;
            memcpy(&value, payload, sizeof(value));
            value = ntohl(value);
            if (value > COMM_INPUT_ACTION)
                return -EPROTO;
            change_local_mode((comm_input_mode)value);
        }
        return 0;

    case MSG_REGISTER_ERROR:
        fprintf(stderr, "Server rejected registration: %s\n", text);
        return -EACCES;

    case MSG_REGISTER_OK:
        return 0;

    default:
        return -EPROTO;
    }
}

static int dispatch_server_packet(int player_id, uint32_t type, const unsigned char *payload,
                                  uint32_t length)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || length > MAX_PAYLOAD)
        return -EINVAL;

    char text[MAX_PAYLOAD + 1U];
    memcpy(text, payload, length);
    text[length] = '\0';

    if (type == MSG_ACTION)
    {
        if (server_action_cb != NULL)
            server_action_cb(player_id, text);
        return 0;
    }

    if (type == MSG_CHAT)
    {
        if (server_chat_cb != NULL)
            server_chat_cb(player_id, text);
        return 0;
    }

    return -EPROTO;
}

static int receive_packets(int fd, recv_state *rx,
                           int (*dispatch)(uint32_t, const unsigned char *, uint32_t))
{
    for (;;)
    {
        if (rx->header_used < sizeof(wire_header))
        {
            ssize_t n = recv(fd, rx->header + rx->header_used,
                             sizeof(wire_header) - rx->header_used, 0);
            if (n > 0)
            {
                rx->header_used += (size_t)n;
                if (rx->header_used < sizeof(wire_header))
                    continue;

                wire_header header;
                memcpy(&header, rx->header, sizeof(header));
                rx->type = ntohl(header.type);
                rx->length = ntohl(header.length);
                if (rx->length > MAX_PAYLOAD)
                    return -EMSGSIZE;
            }
            else if (n == 0)
                return 1;
            else if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            else
                return -errno;
        }

        while (rx->payload_used < rx->length)
        {
            ssize_t n = recv(fd, rx->payload + rx->payload_used,
                             rx->length - rx->payload_used, 0);
            if (n > 0)
                rx->payload_used += (size_t)n;
            else if (n == 0)
                return 1;
            else if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            else
                return -errno;
        }

        int r = dispatch(rx->type, rx->payload, rx->length);
        reset_rx(rx);
        if (r < 0)
            return r;
    }
}

static int player_dispatch_adapter(uint32_t type, const unsigned char *payload, uint32_t length)
{
    return dispatch_player_packet(type, payload, length);
}

static int current_server_player = -1;
static int server_dispatch_adapter(uint32_t type, const unsigned char *payload, uint32_t length)
{
    return dispatch_server_packet(current_server_player, type, payload, length);
}

static int connect_to_server(const char *address)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(address, SERVER_PORT, &hints, &result);
    if (gai != 0)
        return -EHOSTUNREACH;

    int fd = -1;
    for (it = result; it != NULL; it = it->ai_next)
    {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    if (fd < 0)
        return -ECONNREFUSED;
    return fd;
}

int comm_init(int player_id, const char *server_address, message_handler message_cb,
              action_handler action_cb, input_mode_handler mode_cb)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || server_address == NULL || message_cb == NULL ||
        action_cb == NULL || mode_cb == NULL)
        return -EINVAL;

    is_server = 0;
    local_player_id = player_id;
    player_message_cb = message_cb;
    player_action_cb = action_cb;
    player_mode_cb = mode_cb;

    server_fd = connect_to_server(server_address);
    if (server_fd < 0)
        return server_fd;

    uint32_t id = htonl((uint32_t)player_id);
    int r = send_packet(server_fd, MSG_REGISTER, &id, sizeof(id));
    if (r < 0)
        return r;

    reset_rx(&server_rx);
    if ((r = set_nonblocking(server_fd)) < 0)
        return r;

    rl_callback_handler_install("", readline_line_ready);
    change_local_mode(COMM_INPUT_NONE);
    return 0;
}

int comm_send_action(const char *action)
{
    if (server_fd < 0 || action == NULL)
        return -EINVAL;
    return send_string_packet(server_fd, MSG_ACTION, action);
}

int comm_send_chat(const char *message)
{
    if (server_fd < 0 || message == NULL)
        return -EINVAL;
    return send_string_packet(server_fd, MSG_CHAT, message);
}

static int create_listener(void)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int gai = getaddrinfo(NULL, SERVER_PORT, &hints, &result);
    if (gai != 0)
        return -EADDRNOTAVAIL;

    int fd = -1;
    for (it = result; it != NULL; it = it->ai_next)
    {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0)
            continue;

        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        if (bind(fd, it->ai_addr, it->ai_addrlen) == 0 && listen(fd, NUM_PLAYERS) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);
    if (fd < 0)
        return -EADDRINUSE;
    return fd;
}

int comm_server_init(connect_handler connect_cb, server_action_handler action_cb,
                     server_chat_handler chat_cb)
{
    if (connect_cb == NULL || action_cb == NULL || chat_cb == NULL)
        return -EINVAL;

    is_server = 1;
    server_connect_cb = connect_cb;
    server_action_cb = action_cb;
    server_chat_cb = chat_cb;

    listen_fd = create_listener();
    if (listen_fd < 0)
        return listen_fd;

    int r = set_nonblocking(listen_fd);
    if (r < 0)
        return r;

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd < 0)
        return -errno;

    printf("Listening for players on TCP port %s...\n", SERVER_PORT);
    return 0;
}

int comm_send_message(int player_id, const char *message)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || player_fds[player_id] < 0 || message == NULL)
        return -ENOTCONN;
    return send_string_packet(player_fds[player_id], MSG_MESSAGE, message);
}

int comm_request_action(int player_id, const char *prompt)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || player_fds[player_id] < 0 || prompt == NULL)
        return -ENOTCONN;
    return send_string_packet(player_fds[player_id], MSG_ACTION_REQUEST, prompt);
}

int comm_set_input_mode(int player_id, comm_input_mode mode)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || player_fds[player_id] < 0 ||
        mode < COMM_INPUT_NONE || mode > COMM_INPUT_ACTION)
        return -EINVAL;

    uint32_t value = htonl((uint32_t)mode);
    return send_packet(player_fds[player_id], MSG_INPUT_MODE, &value, sizeof(value));
}

int comm_broadcast_input_mode(comm_input_mode mode)
{
    int first_error = 0;
    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        int r = comm_set_input_mode(i, mode);
        if (r < 0 && first_error == 0)
            first_error = r;
    }
    return first_error;
}

int comm_broadcast(const char *message)
{
    if (message == NULL)
        return -EINVAL;

    int first_error = 0;
    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        int r = comm_send_message(i, message);
        if (r < 0 && first_error == 0)
            first_error = r;
    }
    return first_error;
}

int comm_set_timer(unsigned int seconds, timer_handler timer_cb)
{
    if (timer_fd < 0 || timer_cb == NULL)
        return -EINVAL;

    struct itimerspec timer = {0};
    timer.it_value.tv_sec = (time_t)seconds;
    if (seconds == 0)
        timer.it_value.tv_nsec = 1;

    if (timerfd_settime(timer_fd, 0, &timer, NULL) < 0)
        return -errno;

    active_timer_cb = timer_cb;
    return 0;
}

int comm_cancel_timer(void)
{
    if (timer_fd < 0)
        return 0;

    struct itimerspec timer = {0};
    if (timerfd_settime(timer_fd, 0, &timer, NULL) < 0)
        return -errno;

    active_timer_cb = NULL;
    return 0;
}

static int process_completed_line(void)
{
    if (completed_line == NULL)
        return 0;

    char *line = completed_line;
    completed_line = NULL;

    if (local_input_mode == COMM_INPUT_NONE)
    {
        free(line);
        return 0;
    }

    if (line[0] != '\0')
        add_history(line);

    int r = 0;
    if (local_input_mode == COMM_INPUT_CHAT)
    {
        if (line[0] != '\0')
            r = comm_send_chat(line);
    }
    else if (local_input_mode == COMM_INPUT_ACTION)
    {
        r = comm_send_action(line);
        if (r >= 0)
            change_local_mode(COMM_INPUT_NONE);
    }

    free(line);
    return r;
}

static int handle_timer(void)
{
    uint64_t expirations;
    ssize_t bytes = read(timer_fd, &expirations, sizeof(expirations));
    if (bytes < 0)
    {
        if (errno == EAGAIN)
            return 0;
        return -errno;
    }
    if (bytes != (ssize_t)sizeof(expirations))
        return -EIO;

    timer_handler callback = active_timer_cb;
    active_timer_cb = NULL;
    if (callback != NULL)
        callback();
    return 0;
}

static void close_player(int player_id)
{
    if (player_id >= 0 && player_id < NUM_PLAYERS && player_fds[player_id] >= 0)
    {
        close(player_fds[player_id]);
        player_fds[player_id] = -1;
        reset_rx(&player_rx[player_id]);
        fprintf(stderr, "Captain %d disconnected.\n", player_id + 1);
    }
}

static int register_pending(int slot)
{
    recv_state *rx = &pending_rx[slot];
    int fd = pending_fds[slot];

    for (;;)
    {
        if (rx->header_used < sizeof(wire_header))
        {
            ssize_t n = recv(fd, rx->header + rx->header_used,
                             sizeof(wire_header) - rx->header_used, 0);
            if (n > 0)
                rx->header_used += (size_t)n;
            else if (n == 0)
                return 1;
            else if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            else
                return -errno;

            if (rx->header_used < sizeof(wire_header))
                return 0;

            wire_header h;
            memcpy(&h, rx->header, sizeof(h));
            rx->type = ntohl(h.type);
            rx->length = ntohl(h.length);
            if (rx->type != MSG_REGISTER || rx->length != sizeof(uint32_t))
                return -EPROTO;
        }

        while (rx->payload_used < rx->length)
        {
            ssize_t n = recv(fd, rx->payload + rx->payload_used,
                             rx->length - rx->payload_used, 0);
            if (n > 0)
                rx->payload_used += (size_t)n;
            else if (n == 0)
                return 1;
            else if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            else
                return -errno;
        }

        uint32_t network_id;
        memcpy(&network_id, rx->payload, sizeof(network_id));
        uint32_t id = ntohl(network_id);

        if (id >= NUM_PLAYERS || player_fds[id] >= 0)
        {
            const char *reason = (id >= NUM_PLAYERS) ? "Invalid captain number." :
                                                      "That captain is already connected.";
            (void)send_string_packet(fd, MSG_REGISTER_ERROR, reason);
            return 1;
        }

        player_fds[id] = fd;
        player_rx[id] = *rx;
        reset_rx(&player_rx[id]);
        pending_fds[slot] = -1;
        reset_rx(rx);

        (void)send_packet(fd, MSG_REGISTER_OK, NULL, 0);
        if (server_connect_cb != NULL)
            server_connect_cb((int)id);
        return 2;
    }
}

static int accept_connections(void)
{
    for (;;)
    {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            if (errno == EINTR)
                continue;
            return -errno;
        }

        int r = set_nonblocking(fd);
        if (r < 0)
        {
            close(fd);
            continue;
        }

        int slot;
        for (slot = 0; slot < NUM_PLAYERS; slot++)
            if (pending_fds[slot] < 0)
                break;

        if (slot == NUM_PLAYERS)
        {
            (void)send_string_packet(fd, MSG_REGISTER_ERROR, "Server has too many pending clients.");
            close(fd);
            continue;
        }

        pending_fds[slot] = fd;
        reset_rx(&pending_rx[slot]);
    }
}

static int run_server_loop(void)
{
    for (;;)
    {
        struct pollfd fds[2 + NUM_PLAYERS * 2];
        int kinds[2 + NUM_PLAYERS * 2];
        int ids[2 + NUM_PLAYERS * 2];
        nfds_t count = 0;

        fds[count] = (struct pollfd){.fd = listen_fd, .events = POLLIN};
        kinds[count] = 0;
        ids[count++] = -1;
        fds[count] = (struct pollfd){.fd = timer_fd, .events = POLLIN};
        kinds[count] = 1;
        ids[count++] = -1;

        for (int i = 0; i < NUM_PLAYERS; i++)
        {
            if (player_fds[i] >= 0)
            {
                fds[count] = (struct pollfd){.fd = player_fds[i], .events = POLLIN};
                kinds[count] = 2;
                ids[count++] = i;
            }
            if (pending_fds[i] >= 0)
            {
                fds[count] = (struct pollfd){.fd = pending_fds[i], .events = POLLIN};
                kinds[count] = 3;
                ids[count++] = i;
            }
        }

        int r = poll(fds, count, -1);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        for (nfds_t n = 0; n < count; n++)
        {
            if (fds[n].revents == 0)
                continue;

            if (kinds[n] == 0 && (fds[n].revents & POLLIN))
            {
                r = accept_connections();
                if (r < 0)
                    return r;
            }
            else if (kinds[n] == 1 && (fds[n].revents & POLLIN))
            {
                r = handle_timer();
                if (r < 0)
                    return r;
            }
            else if (kinds[n] == 2)
            {
                int id = ids[n];
                if (fds[n].revents & POLLIN)
                {
                    current_server_player = id;
                    r = receive_packets(player_fds[id], &player_rx[id], server_dispatch_adapter);
                    current_server_player = -1;
                    if (r != 0)
                        close_player(id);
                }
                if (fds[n].revents & (POLLHUP | POLLERR | POLLNVAL))
                    close_player(id);
            }
            else if (kinds[n] == 3)
            {
                int slot = ids[n];
                int fd = pending_fds[slot];
                if (fd < 0)
                    continue;
                if (fds[n].revents & POLLIN)
                {
                    r = register_pending(slot);
                    if (r == 1 || r < 0)
                    {
                        close(fd);
                        pending_fds[slot] = -1;
                        reset_rx(&pending_rx[slot]);
                    }
                }
                if (pending_fds[slot] >= 0 &&
                    (fds[n].revents & (POLLHUP | POLLERR | POLLNVAL)))
                {
                    close(pending_fds[slot]);
                    pending_fds[slot] = -1;
                    reset_rx(&pending_rx[slot]);
                }
            }
        }
    }
}

static int run_player_loop(void)
{
    for (;;)
    {
        struct pollfd fds[2] = {
            {.fd = server_fd, .events = POLLIN, .revents = 0},
            {.fd = STDIN_FILENO,
             .events = (local_input_mode == COMM_INPUT_NONE) ? 0 : POLLIN,
             .revents = 0}};

        int r = process_completed_line();
        if (r < 0)
            return r;
        if (stdin_eof)
            return 0;

        r = poll(fds, 2, -1);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        if (fds[0].revents & POLLIN)
        {
            r = receive_packets(server_fd, &server_rx, player_dispatch_adapter);
            if (r == 1)
            {
                fprintf(stderr, "Server disconnected.\n");
                return 0;
            }
            if (r < 0)
                return r;
        }

        if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL))
        {
            fprintf(stderr, "Server disconnected.\n");
            return 0;
        }

        if (local_input_mode != COMM_INPUT_NONE && (fds[1].revents & POLLIN))
            rl_callback_read_char();

        if ((fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) && !(fds[1].revents & POLLIN))
            return 0;
    }
}

int comm_run(void) { return run_player_loop(); }
int comm_server_run(void) { return run_server_loop(); }

void comm_close(void)
{
    if (!is_server && local_player_id >= 0)
        rl_callback_handler_remove();

    free(completed_line);
    completed_line = NULL;

    if (server_fd >= 0)
    {
        close(server_fd);
        server_fd = -1;
    }
    if (listen_fd >= 0)
    {
        close(listen_fd);
        listen_fd = -1;
    }
    if (timer_fd >= 0)
    {
        close(timer_fd);
        timer_fd = -1;
    }

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        if (player_fds[i] >= 0)
            close(player_fds[i]);
        if (pending_fds[i] >= 0)
            close(pending_fds[i]);
        player_fds[i] = -1;
        pending_fds[i] = -1;
    }

    active_timer_cb = NULL;
    local_player_id = -1;
    local_input_mode = COMM_INPUT_NONE;
    stdin_eof = 0;
}
