#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

#include <systemd/sd-bus.h>

#include "comm.h"

#define SERVER_NAME "edu.asu.Convoy.Server"
#define SERVER_PATH "/edu/asu/Convoy/Server"
#define SERVER_INTERFACE "edu.asu.Convoy.Server"
#define PLAYER_INTERFACE "edu.asu.Convoy.Player"

#define NUM_PLAYERS 4

static sd_bus *bus = NULL;

static int timer_fd = -1;
static timer_handler active_timer_cb = NULL;

static int local_player_id = -1;
static comm_input_mode local_input_mode = COMM_INPUT_NONE;

/* Server-side authenticated identity for each captain. */
static uid_t player_uids[NUM_PLAYERS];
static char *player_senders[NUM_PLAYERS];
static int player_registered[NUM_PLAYERS];

static message_handler player_message_cb = NULL;
static action_handler player_action_cb = NULL;
static input_mode_handler player_mode_cb = NULL;

static connect_handler server_connect_cb = NULL;
static server_action_handler server_action_cb = NULL;
static server_chat_handler server_chat_cb = NULL;

static int stdin_eof = 0;
static char *completed_line = NULL;

static void player_object_path(int id, char *buffer, size_t size)
{
    snprintf(buffer, size, "/edu/asu/Convoy/Player%d", id);
}

static int sender_euid(sd_bus_message *message, uid_t *uid)
{
    sd_bus_creds *creds = NULL;
    int r = sd_bus_query_sender_creds(message, SD_BUS_CREDS_EUID, &creds);

    if (r < 0)
        return r;

    r = sd_bus_creds_get_euid(creds, uid);
    sd_bus_creds_unref(creds);
    return r;
}

static int uid_matches_player(uid_t uid, uint32_t player_id)
{
    char username[32];

    if (player_id >= NUM_PLAYERS)
        return 0;

    snprintf(username, sizeof(username), "player%u", player_id + 1U);

    errno = 0;
    struct passwd *account = getpwnam(username);

    if (account == NULL)
        return 0;

    return account->pw_uid == uid;
}

static int authenticate_player(sd_bus_message *message, uint32_t player_id)
{
    if (player_id >= NUM_PLAYERS)
        return -EINVAL;

    const char *sender = sd_bus_message_get_sender(message);
    uid_t uid;
    int r = sender_euid(message, &uid);

    if (r < 0 || sender == NULL)
        return (r < 0) ? r : -EPERM;

    if (!player_registered[player_id] || player_uids[player_id] != uid ||
        player_senders[player_id] == NULL || strcmp(player_senders[player_id], sender) != 0)
        return -EPERM;

    return 0;
}

static void change_local_mode(comm_input_mode mode)
{
    local_input_mode = mode;

    if (player_mode_cb != NULL)
        player_mode_cb(mode);
}

static void readline_line_ready(char *line)
{
    if (line == NULL)
    {
        stdin_eof = 1;
        return;
    }

    /* Remove Readline's local echo. The server will send back the
     * canonical formatted chat line, e.g. "Captain 2: hello". */
    printf("\033[1A\r\033[2K");
    fflush(stdout);

    if (completed_line != NULL)
        free(completed_line);

    completed_line = line;
}

static int dbus_player_message(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    const char *text;

    int r = sd_bus_message_read(message, "s", &text);

    if (r < 0)
        return r;

    if (player_message_cb != NULL)
        player_message_cb(text);

    return sd_bus_reply_method_return(message, "");
}

static int dbus_player_request_action(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    const char *prompt;

    int r = sd_bus_message_read(message, "s", &prompt);

    if (r < 0)
        return r;

    change_local_mode(COMM_INPUT_ACTION);

    if (player_action_cb != NULL)
        player_action_cb(prompt);

    return sd_bus_reply_method_return(message, "");
}

static int dbus_player_set_input_mode(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    uint32_t mode_value;

    int r = sd_bus_message_read(message, "u", &mode_value);

    if (r < 0)
        return r;

    if (mode_value > COMM_INPUT_ACTION)
    {
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Invalid input mode");
    }

    change_local_mode((comm_input_mode)mode_value);

    return sd_bus_reply_method_return(message, "");
}

static const sd_bus_vtable player_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD("Message", "s", "", dbus_player_message, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD("RequestAction", "s", "", dbus_player_request_action, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD("SetInputMode", "u", "", dbus_player_set_input_mode, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_VTABLE_END};

static int dbus_server_register(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    uint32_t requested_id;

    int r = sd_bus_message_read(message, "u", &requested_id);

    if (r < 0)
        return r;

    if (requested_id >= NUM_PLAYERS)
    {
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Invalid player ID");
    }

    const char *sender = sd_bus_message_get_sender(message);
    uid_t uid;

    r = sender_euid(message, &uid);
    if (r < 0 || sender == NULL)
        return (r < 0) ? r : -EPERM;

    /* Captain N must be run by Linux account playerN. */
    if (!uid_matches_player(uid, requested_id))
    {
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED,
                                          "Captain %u must be run by Linux user player%u",
                                          requested_id + 1U, requested_id + 1U);
    }

    /* One Linux UID may own exactly one captain identity. */
    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        if (player_registered[i] && player_uids[i] == uid && i != (int)requested_id)
        {
            return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED,
                                              "This Linux user is already registered as Captain %d",
                                              i + 1);
        }
    }

    if (player_registered[requested_id])
    {
        if (player_uids[requested_id] != uid || player_senders[requested_id] == NULL ||
            strcmp(player_senders[requested_id], sender) != 0)
        {
            return sd_bus_reply_method_errorf(
                message, SD_BUS_ERROR_ACCESS_DENIED,
                "Captain %u is already owned by another Linux user/process", requested_id + 1U);
        }
    }
    else
    {
        player_senders[requested_id] = strdup(sender);
        if (player_senders[requested_id] == NULL)
            return -ENOMEM;
        player_uids[requested_id] = uid;
        player_registered[requested_id] = 1;

        if (server_connect_cb != NULL)
            server_connect_cb((int)requested_id);
    }

    return sd_bus_reply_method_return(message, "");
}

static int dbus_server_action(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    uint32_t player_id;
    const char *action;

    int r = sd_bus_message_read(message, "us", &player_id, &action);

    if (r < 0)
        return r;

    if (player_id >= NUM_PLAYERS)
    {
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Invalid player ID");
    }

    r = authenticate_player(message, player_id);
    if (r < 0)
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED,
                                          "Sender is not authenticated as Captain %u",
                                          player_id + 1U);

    if (server_action_cb != NULL)
    {
        server_action_cb((int)player_id, action);
    }

    return sd_bus_reply_method_return(message, "");
}

static int dbus_server_chat(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    uint32_t player_id;
    const char *text;

    int r = sd_bus_message_read(message, "us", &player_id, &text);

    if (r < 0)
        return r;

    if (player_id >= NUM_PLAYERS)
    {
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS, "Invalid player ID");
    }

    r = authenticate_player(message, player_id);
    if (r < 0)
        return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_ACCESS_DENIED,
                                          "Sender is not authenticated as Captain %u",
                                          player_id + 1U);

    if (server_chat_cb != NULL)
    {
        server_chat_cb((int)player_id, text);
    }

    return sd_bus_reply_method_return(message, "");
}

static const sd_bus_vtable server_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD("Register", "u", "", dbus_server_register, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD("Action", "us", "", dbus_server_action, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD("Chat", "us", "", dbus_server_chat, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_VTABLE_END};

static int ignore_method_reply(sd_bus_message *message, void *userdata, sd_bus_error *error)
{
    (void)message;
    (void)userdata;
    (void)error;

    return 0;
}

int comm_init(int player_id, message_handler message_cb, action_handler action_cb,
              input_mode_handler mode_cb)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || message_cb == NULL || action_cb == NULL ||
        mode_cb == NULL)
    {
        return -EINVAL;
    }

    local_player_id = player_id;

    player_message_cb = message_cb;
    player_action_cb = action_cb;
    player_mode_cb = mode_cb;

    int r = sd_bus_open_system(&bus);

    if (r < 0)
        return r;

    char path[128];

    player_object_path(player_id, path, sizeof(path));

    r = sd_bus_add_object_vtable(bus, NULL, path, PLAYER_INTERFACE, player_vtable, NULL);

    if (r < 0)
        return r;

    r = sd_bus_call_method(bus, SERVER_NAME, SERVER_PATH, SERVER_INTERFACE, "Register", NULL, NULL,
                           "u", (uint32_t)player_id);

    if (r < 0)
        return r;

    rl_callback_handler_install("", readline_line_ready);

    local_input_mode = COMM_INPUT_NONE;
    change_local_mode(COMM_INPUT_NONE);

    return 0;
}

int comm_send_action(const char *action)
{
    if (bus == NULL || local_player_id < 0 || action == NULL)
    {
        return -EINVAL;
    }

    return sd_bus_call_method_async(bus, NULL, SERVER_NAME, SERVER_PATH, SERVER_INTERFACE, "Action",
                                    ignore_method_reply, NULL, "us", (uint32_t)local_player_id,
                                    action);
}

int comm_send_chat(const char *message)
{
    if (bus == NULL || local_player_id < 0 || message == NULL)
    {
        return -EINVAL;
    }

    return sd_bus_call_method_async(bus, NULL, SERVER_NAME, SERVER_PATH, SERVER_INTERFACE, "Chat",
                                    ignore_method_reply, NULL, "us", (uint32_t)local_player_id,
                                    message);
}

int comm_server_init(connect_handler connect_cb, server_action_handler action_cb,
                     server_chat_handler chat_cb)
{
    if (connect_cb == NULL || action_cb == NULL || chat_cb == NULL)
    {
        return -EINVAL;
    }

    server_connect_cb = connect_cb;
    server_action_cb = action_cb;
    server_chat_cb = chat_cb;

    int r = sd_bus_open_system(&bus);

    if (r < 0)
        return r;

    r = sd_bus_request_name(bus, SERVER_NAME, 0);

    if (r < 0)
        return r;

    r = sd_bus_add_object_vtable(bus, NULL, SERVER_PATH, SERVER_INTERFACE, server_vtable, NULL);

    if (r < 0)
        return r;

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    if (timer_fd < 0)
        return -errno;

    return 0;
}

int comm_send_message(int player_id, const char *message)
{
    if (bus == NULL || player_id < 0 || player_id >= NUM_PLAYERS || message == NULL)
    {
        return -EINVAL;
    }

    if (!player_registered[player_id] || player_senders[player_id] == NULL)
        return -ENOTCONN;

    char path[128];

    player_object_path(player_id, path, sizeof(path));

    return sd_bus_call_method_async(bus, NULL, player_senders[player_id], path, PLAYER_INTERFACE,
                                    "Message", ignore_method_reply, NULL, "s", message);
}

int comm_request_action(int player_id, const char *prompt)
{
    if (bus == NULL || player_id < 0 || player_id >= NUM_PLAYERS || prompt == NULL)
    {
        return -EINVAL;
    }

    if (!player_registered[player_id] || player_senders[player_id] == NULL)
        return -ENOTCONN;

    char path[128];

    player_object_path(player_id, path, sizeof(path));

    return sd_bus_call_method_async(bus, NULL, player_senders[player_id], path, PLAYER_INTERFACE,
                                    "RequestAction", ignore_method_reply, NULL, "s", prompt);
}

int comm_set_input_mode(int player_id, comm_input_mode mode)
{
    if (bus == NULL || player_id < 0 || player_id >= NUM_PLAYERS || mode < COMM_INPUT_NONE ||
        mode > COMM_INPUT_ACTION)
    {
        return -EINVAL;
    }

    if (!player_registered[player_id] || player_senders[player_id] == NULL)
        return -ENOTCONN;

    char path[128];

    player_object_path(player_id, path, sizeof(path));

    return sd_bus_call_method_async(bus, NULL, player_senders[player_id], path, PLAYER_INTERFACE,
                                    "SetInputMode", ignore_method_reply, NULL, "u", (uint32_t)mode);
}

int comm_broadcast_input_mode(comm_input_mode mode)
{
    int first_error = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        int r = comm_set_input_mode(i, mode);

        if (r < 0 && first_error == 0)
        {
            first_error = r;
        }
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
        {
            first_error = r;
        }
    }

    return first_error;
}

int comm_set_timer(unsigned int seconds, timer_handler timer_cb)
{
    if (timer_fd < 0 || timer_cb == NULL)
    {
        return -EINVAL;
    }

    struct itimerspec timer = {0};

    timer.it_value.tv_sec = (time_t)seconds;

    if (seconds == 0)
        timer.it_value.tv_nsec = 1;

    if (timerfd_settime(timer_fd, 0, &timer, NULL) < 0)
    {
        return -errno;
    }

    active_timer_cb = timer_cb;

    return 0;
}

int comm_cancel_timer(void)
{
    if (timer_fd < 0)
        return 0;

    struct itimerspec timer = {0};

    if (timerfd_settime(timer_fd, 0, &timer, NULL) < 0)
    {
        return -errno;
    }

    active_timer_cb = NULL;

    return 0;
}

static int bus_timeout_ms(void)
{
    uint64_t timeout_usec;

    int r = sd_bus_get_timeout(bus, &timeout_usec);

    if (r < 0 || timeout_usec == UINT64_MAX)
    {
        return -1;
    }

    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
        return -1;
    }

    uint64_t now_usec = (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_nsec / 1000ULL;

    if (timeout_usec <= now_usec)
        return 0;

    uint64_t delta_usec = timeout_usec - now_usec;

    uint64_t delta_ms = (delta_usec + 999ULL) / 1000ULL;

    if (delta_ms > INT32_MAX)
        return INT32_MAX;

    return (int)delta_ms;
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

    if (r < 0)
        return r;

    return 0;
}

static int process_bus(void)
{
    int r;

    while ((r = sd_bus_process(bus, NULL)) > 0)
    {
    }

    return r;
}

static int handle_timer(void)
{
    if (timer_fd < 0)
        return 0;

    uint64_t expirations;

    ssize_t bytes = read(timer_fd, &expirations, sizeof(expirations));

    if (bytes < 0)
    {
        if (errno == EAGAIN)
            return 0;

        return -errno;
    }

    if (bytes != (ssize_t)sizeof(expirations))
    {
        return -EIO;
    }

    timer_handler callback = active_timer_cb;

    active_timer_cb = NULL;

    if (callback != NULL)
        callback();

    return 0;
}

static int run_event_loop(void)
{
    if (bus == NULL)
        return -EINVAL;

    for (;;)
    {
        int r = process_bus();

        if (r < 0)
            return r;

        r = process_completed_line();

        if (r < 0)
            return r;

        if (stdin_eof)
            return 0;

        int bus_fd = sd_bus_get_fd(bus);

        if (bus_fd < 0)
            return bus_fd;

        int bus_events = sd_bus_get_events(bus);

        if (bus_events < 0)
            return bus_events;

        struct pollfd fds[3] = {
            {
                .fd = bus_fd,
                .events = (short)bus_events,
                .revents = 0,
            },
            {
                .fd = timer_fd,
                .events = POLLIN,
                .revents = 0,
            },
            {
                .fd = (local_player_id >= 0) ? STDIN_FILENO : -1,
                .events = (local_input_mode == COMM_INPUT_NONE) ? 0 : POLLIN,
                .revents = 0,
            },
        };

        r = poll(fds, 3U, bus_timeout_ms());

        if (r < 0)
        {
            if (errno == EINTR)
                continue;

            return -errno;
        }

        if (timer_fd >= 0 && (fds[1].revents & POLLIN) != 0)
        {
            r = handle_timer();

            if (r < 0)
                return r;
        }

        if (local_player_id >= 0 && local_input_mode != COMM_INPUT_NONE &&
            (fds[2].revents & POLLIN) != 0)
        {
            rl_callback_read_char();
        }

        if (local_player_id >= 0 && (fds[2].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 &&
            (fds[2].revents & POLLIN) == 0)
        {
            return 0;
        }
    }
}

int comm_run(void) { return run_event_loop(); }

int comm_server_run(void) { return run_event_loop(); }

void comm_close(void)
{
    if (local_player_id >= 0)
        rl_callback_handler_remove();

    if (completed_line != NULL)
    {
        free(completed_line);
        completed_line = NULL;
    }

    if (timer_fd >= 0)
    {
        close(timer_fd);
        timer_fd = -1;
    }

    active_timer_cb = NULL;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        free(player_senders[i]);
        player_senders[i] = NULL;
        player_registered[i] = 0;
        player_uids[i] = (uid_t)0;
    }

    if (bus != NULL)
    {
        sd_bus_flush_close_unref(bus);
        bus = NULL;
    }

    local_player_id = -1;
    local_input_mode = COMM_INPUT_NONE;
    stdin_eof = 0;
}
