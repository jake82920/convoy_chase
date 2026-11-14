/*
 * comm.c
 *
 * All D-Bus communication for Naval Convoy.
 *
 * Compile/link with:
 *     pkg-config --cflags --libs libsystemd
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include "comm.h"

#define SERVER_NAME "edu.asu.Convoy.Server"
#define SERVER_PATH "/edu/asu/Convoy/Server"
#define SERVER_INTERFACE "edu.asu.Convoy.Server"

#define PLAYER_INTERFACE "edu.asu.Convoy.Player"

static sd_bus *bus = NULL;

/* ============================================================
 * Shared state
 * ============================================================ */

static int local_player_id = -1;

static message_handler player_message_cb = NULL;
static action_handler player_action_cb = NULL;

static connect_handler server_connect_cb = NULL;
static server_action_handler server_action_cb = NULL;

/* ============================================================
 * Helpers
 * ============================================================ */

static void player_bus_name(int id, char *buffer, size_t size)
{
    snprintf(buffer, size, "edu.asu.Convoy.Player%d", id);
}

static void player_object_path(int id, char *buffer, size_t size)
{
    snprintf(buffer, size, "/edu/asu/Convoy/Player%d", id);
}

/* ============================================================
 * CLIENT-SIDE D-BUS METHODS
 *
 * These are called by the server.
 * ============================================================ */

static int dbus_player_message(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    const char *text;

    int r = sd_bus_message_read(m, "s", &text);

    if (r < 0)
        return r;

    if (player_message_cb != NULL)
        player_message_cb(text);

    return sd_bus_reply_method_return(m, "");
}

static int dbus_player_request_action(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    const char *prompt;

    int r = sd_bus_message_read(m, "s", &prompt);

    if (r < 0)
        return r;

    if (player_action_cb != NULL)
        player_action_cb(prompt);

    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable player_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD("Message", "s", "", dbus_player_message, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD("RequestAction", "s", "", dbus_player_request_action, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_VTABLE_END};

/* ============================================================
 * SERVER-SIDE D-BUS METHODS
 *
 * These are called by players.
 * ============================================================ */

static int dbus_server_register(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    uint32_t requested_id;

    int r = sd_bus_message_read(m, "u", &requested_id);

    if (r < 0)
        return r;

    if (requested_id >= 4)
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "Invalid player ID");

    if (server_connect_cb != NULL)
        server_connect_cb((int)requested_id);

    return sd_bus_reply_method_return(m, "");
}

static int dbus_server_action(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    (void)userdata;
    (void)error;

    uint32_t player_id;
    const char *action;

    int r = sd_bus_message_read(m, "us", &player_id, &action);

    if (r < 0)
        return r;

    if (player_id >= 4)
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "Invalid player ID");

    if (server_action_cb != NULL)
        server_action_cb((int)player_id, action);

    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable server_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_METHOD("Register", "u", "", dbus_server_register, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_METHOD("Action", "us", "", dbus_server_action, SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_VTABLE_END};

static int ignore_method_reply(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    (void)m;
    (void)userdata;
    (void)error;
    return 0;
}

/* ============================================================
 * PLAYER API
 * ============================================================ */

int comm_init(int player_id, message_handler message_cb, action_handler action_cb)
{
    if (player_id < 0 || player_id >= 4 || message_cb == NULL || action_cb == NULL)
        return -1;

    int r;

    local_player_id = player_id;
    player_message_cb = message_cb;
    player_action_cb = action_cb;

    r = sd_bus_open_user(&bus);

    if (r < 0)
        return r;

    char name[128];
    char path[128];

    player_bus_name(player_id, name, sizeof(name));

    player_object_path(player_id, path, sizeof(path));

    r = sd_bus_request_name(bus, name, 0);

    if (r < 0)
        return r;

    r = sd_bus_add_object_vtable(bus, NULL, path, PLAYER_INTERFACE, player_vtable, NULL);

    if (r < 0)
        return r;

    /*
     * Tell the server that this player exists.
     */
    r = sd_bus_call_method(bus, SERVER_NAME, SERVER_PATH, SERVER_INTERFACE, "Register", NULL, NULL,
                           "u", (uint32_t)player_id);

    return r;
}

int comm_send_action(const char *action)
{
    if (bus == NULL || local_player_id < 0 || action == NULL)
        return -1;

    return sd_bus_call_method_async(bus, NULL, SERVER_NAME, SERVER_PATH, SERVER_INTERFACE, "Action",
                                    ignore_method_reply, NULL, "us", (uint32_t)local_player_id,
                                    action);
}

/* ============================================================
 * SERVER API
 * ============================================================ */

int comm_server_init(connect_handler connect_cb, server_action_handler action_cb)
{
    if (connect_cb == NULL || action_cb == NULL)
        return -1;

    int r;

    server_connect_cb = connect_cb;
    server_action_cb = action_cb;

    r = sd_bus_open_user(&bus);

    if (r < 0)
        return r;

    r = sd_bus_request_name(bus, SERVER_NAME, 0);

    if (r < 0)
        return r;

    return sd_bus_add_object_vtable(bus, NULL, SERVER_PATH, SERVER_INTERFACE, server_vtable, NULL);
}

int comm_send_message(int player_id, const char *message)
{
    char name[128];
    char path[128];

    player_bus_name(player_id, name, sizeof(name));

    player_object_path(player_id, path, sizeof(path));

    return sd_bus_call_method_async(bus, NULL, name, path, PLAYER_INTERFACE, "Message",
                                    ignore_method_reply, NULL, "s", message);
}

int comm_request_action(int player_id, const char *prompt)
{
    char name[128];
    char path[128];

    player_bus_name(player_id, name, sizeof(name));

    player_object_path(player_id, path, sizeof(path));

    return sd_bus_call_method_async(bus, NULL, name, path, PLAYER_INTERFACE, "RequestAction",
                                    ignore_method_reply, NULL, "s", prompt);
}

int comm_broadcast(const char *message)
{
    int first_error = 0;

    for (int i = 0; i < 4; i++)
    {

        int r = comm_send_message(i, message);

        /*
         * A player may not have connected yet.
         * Record the error but continue broadcasting
         * to everybody else.
         */
        if (r < 0 && first_error == 0)
            first_error = r;
    }

    return first_error;
}

/* ============================================================
 * EVENT LOOP
 * ============================================================ */

static int run_event_loop(void)
{
    if (bus == NULL)
        return -1;

    while (1)
    {

        int r = sd_bus_process(bus, NULL);

        if (r < 0)
            return r;

        if (r > 0)
            continue;

        r = sd_bus_wait(bus, UINT64_MAX);

        if (r < 0)
            return r;
    }
}

int comm_run(void) { return run_event_loop(); }

int comm_server_run(void) { return run_event_loop(); }

/* ============================================================
 * CLEANUP
 * ============================================================ */

void comm_close(void)
{
    if (bus != NULL)
    {
        sd_bus_flush_close_unref(bus);
        bus = NULL;
    }
}
