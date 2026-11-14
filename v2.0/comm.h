#ifndef COMM_H
#define COMM_H

typedef enum
{
    COMM_INPUT_NONE = 0,
    COMM_INPUT_CHAT = 1,
    COMM_INPUT_ACTION = 2
} comm_input_mode;

typedef void (*message_handler)(const char *message);
typedef void (*action_handler)(const char *prompt);
typedef void (*input_mode_handler)(comm_input_mode mode);

typedef void (*connect_handler)(int player_id);
typedef void (*server_action_handler)(int player_id, const char *action);
typedef void (*server_chat_handler)(int player_id, const char *message);

typedef void (*timer_handler)(void);

/* Player side */
int comm_init(int player_id, const char *server_address, message_handler message_cb,
              action_handler action_cb, input_mode_handler mode_cb);
int comm_run(void);
int comm_send_action(const char *action);
int comm_send_chat(const char *message);

/* Server side */
int comm_server_init(connect_handler connect_cb, server_action_handler action_cb,
                     server_chat_handler chat_cb);
int comm_server_run(void);

int comm_send_message(int player_id, const char *message);
int comm_request_action(int player_id, const char *prompt);
int comm_set_input_mode(int player_id, comm_input_mode mode);
int comm_broadcast_input_mode(comm_input_mode mode);
int comm_broadcast(const char *message);

/* Server timer */
int comm_set_timer(unsigned int seconds, timer_handler timer_cb);
int comm_cancel_timer(void);

/* Shared */
void comm_close(void);

#endif
