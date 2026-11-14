#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"

#define PLAYER_MAX_INPUT 256
#define NUM_PLAYERS 4

static void get_input(char *buffer, size_t size)
{
    if (size == 0)
        return;

    if (size > INT_MAX)
        size = INT_MAX;

    if (fgets(buffer, (int)size, stdin) == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    buffer[strcspn(buffer, "\n")] = '\0';
}

static void handle_message(const char *message)
{
    printf("%s\n", message);
    fflush(stdout);
}

static void handle_action_request(const char *prompt)
{
    char input[PLAYER_MAX_INPUT];

    printf("\n%s\n> ", prompt);
    fflush(stdout);

    get_input(input, sizeof(input));

    int r = comm_send_action(input);

    if (r < 0)
        fprintf(stderr, "Failed to send action: %d\n", r);
}

static int parse_player_number(const char *text, int *player_id)
{
    char *end = NULL;
    errno = 0;
    long number = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || number < 1 || number > NUM_PLAYERS)
        return -1;

    *player_id = (int)number - 1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <captain 1-%d>\n", argv[0], NUM_PLAYERS);
        return EXIT_FAILURE;
    }

    int player_id;

    if (parse_player_number(argv[1], &player_id) < 0)
    {
        fprintf(stderr, "Captain number must be between 1 and %d.\n", NUM_PLAYERS);
        return EXIT_FAILURE;
    }

    printf("=================================\n");
    printf("       NAVAL CONVOY\n");
    printf("=================================\n\n");

    int r = comm_init(player_id, handle_message, handle_action_request);

    if (r < 0)
    {
        fprintf(stderr, "Could not initialize communications: %d\n", r);
        comm_close();
        return EXIT_FAILURE;
    }

    printf("Connected as Captain %d. Waiting for the fleet...\n", player_id + 1);

    r = comm_run();
    comm_close();

    if (r < 0)
    {
        fprintf(stderr, "Communication error: %d\n", r);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
