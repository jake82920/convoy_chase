#ifndef COMM_H
#define COMM_H

typedef void (*message_handler)(const char *message);

typedef void (*action_handler)(const char *prompt);

typedef void (*connect_handler)(int player_id);

typedef void (*server_action_handler)(int player_id, const char *action);

/* Player side */

int comm_init(int player_id, message_handler message_cb, action_handler action_cb);

int comm_send_action(const char *action);

int comm_run(void);

/* Server side */

int comm_server_init(connect_handler connect_cb, server_action_handler action_cb);

int comm_send_message(int player_id, const char *message);

int comm_request_action(int player_id, const char *prompt);

int comm_broadcast(const char *message);

int comm_server_run(void);

/* Shared */

void comm_close(void);

#endif
