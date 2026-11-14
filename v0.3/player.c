#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>

#include "comm.h"

#define NUM_PLAYERS 4

static comm_input_mode input_mode = COMM_INPUT_NONE;

static void redraw_prompt(void)
{
    rl_set_prompt(input_mode == COMM_INPUT_NONE ? "" : "> ");
    rl_on_new_line();
    rl_redisplay();
}

static void print_async(const char *text)
{
    printf("\r\033[2K%s\n", text);
    fflush(stdout);

    if (input_mode != COMM_INPUT_NONE)
        redraw_prompt();
}

static void handle_message(const char *message)
{
    print_async(message);
}

static void handle_action_request(const char *prompt)
{
    input_mode = COMM_INPUT_ACTION;

    printf("\r\033[2K%s\n", prompt);
    fflush(stdout);

    redraw_prompt();
}

static void handle_input_mode(comm_input_mode mode)
{
    input_mode = mode;

    if (mode == COMM_INPUT_NONE) {
        rl_replace_line("", 0);
        printf("\r\033[2K");
        fflush(stdout);
        return;
    }

    redraw_prompt();
}

static int parse_player_number(const char *text, int *player_id)
{
    char *end = NULL;
    errno = 0;

    long number = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' ||
        number < 1 || number > NUM_PLAYERS)
        return -1;

    *player_id = (int)number - 1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <captain 1-%d>\n", argv[0], NUM_PLAYERS);
        return EXIT_FAILURE;
    }

    int player_id;

    if (parse_player_number(argv[1], &player_id) < 0) {
        fprintf(stderr, "Captain number must be between 1 and %d.\n", NUM_PLAYERS);
        return EXIT_FAILURE;
    }

    printf("=================================\n");
    printf("          CONVOY CHASE\n");
    printf("=================================\n\n");

    rl_readline_name = "convoy-chase";

    int r = comm_init(
        player_id,
        handle_message,
        handle_action_request,
        handle_input_mode);

    if (r < 0) {
        fprintf(stderr, "Could not initialize communications: %d\n", r);
        comm_close();
        return EXIT_FAILURE;
    }

    printf("Connected as Captain %d.\n", player_id + 1);
    printf("Waiting for the fleet...\n");

    r = comm_run();
    comm_close();

    if (r < 0) {
        fprintf(stderr, "Communication error: %d\n", r);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

