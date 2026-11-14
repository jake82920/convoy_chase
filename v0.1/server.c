#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "comm.h"

#define NUM_PLAYERS 4
#define STARTING_SUPPLIES 6
#define MAX_SUPPLIES 6
#define ANCHOR_RECOVERY 3

#define PROGRESS_TO_WIN 4
#define DELAY_TO_WIN 4
#define REJECTIONS_FOR_DELAY 3
#define MISSION_REQUIREMENT 6

typedef enum
{
    ROLE_NAVY,
    ROLE_PIRATE
} Role;

typedef enum
{
    PHASE_WAITING,
    PHASE_PROPOSAL,
    PHASE_CREW_SELECTION,
    PHASE_VOTING,
    PHASE_MISSION,
    PHASE_GAME_OVER
} Phase;

typedef enum
{
    PROPOSAL_NONE,
    PROPOSAL_SAIL,
    PROPOSAL_ANCHOR
} ProposalType;

typedef struct
{
    int id;
    Role role;
    int supplies;
    bool connected;

    bool voted;
    bool vote_approve;

    bool selected;
    bool contributed;
    int contribution;
} Player;

typedef struct
{
    Player players[NUM_PLAYERS];

    Phase phase;
    ProposalType proposal;

    int connected_players;
    int commodore;

    int convoy_progress;
    int pirate_delay;
    int rejected_proposals;

    int crew_size;
} Game;

static Game game;

static bool parse_crew(const char *input);

/* ---------- Utility ---------- */

static void broadcast_status(void)
{
    char buffer[256];

    snprintf(buffer, sizeof(buffer),
             "\n=== VOYAGE STATUS ===\n"
             "Convoy progress: %d/%d\n"
             "Pirate delay:    %d/%d\n",
             game.convoy_progress, PROGRESS_TO_WIN, game.pirate_delay, DELAY_TO_WIN);

    comm_broadcast(buffer);
}

static void reset_votes(void)
{
    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        game.players[i].voted = false;
        game.players[i].vote_approve = false;
    }
}

static void reset_mission(void)
{
    game.crew_size = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        game.players[i].selected = false;
        game.players[i].contributed = false;
        game.players[i].contribution = 0;
    }
}

static void next_commodore(void) { game.commodore = (game.commodore + 1) % NUM_PLAYERS; }

/* ---------- Victory ---------- */

static bool check_victory(void)
{
    if (game.convoy_progress >= PROGRESS_TO_WIN)
    {
        comm_broadcast("\nThe convoy has reached port safely.\n"
                       "ROYAL NAVY VICTORY!\n");

        game.phase = PHASE_GAME_OVER;
        return true;
    }

    if (game.pirate_delay >= DELAY_TO_WIN)
    {
        comm_broadcast("\nBlack sails appear on the horizon.\n"
                       "The convoy has been delayed too long.\n"
                       "PIRATE VICTORY!\n");

        game.phase = PHASE_GAME_OVER;
        return true;
    }

    return false;
}

/* ---------- Roles ---------- */

static void assign_roles(void)
{
    int pirate = rand() % NUM_PLAYERS;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        Player *p = &game.players[i];

        p->role = (i == pirate) ? ROLE_PIRATE : ROLE_NAVY;
        p->supplies = STARTING_SUPPLIES;

        if (p->role == ROLE_PIRATE)
        {
            comm_send_message(p->id, "You are the PIRATE INFILTRATOR.\n"
                                     "Delay the convoy until your fleet catches it.");
        }
        else
        {
            comm_send_message(p->id, "You are a ROYAL NAVY captain.\n"
                                     "Get the convoy safely to port.");
        }
    }
}

/* ---------- Round ---------- */

static void begin_round(void)
{
    if (check_victory())
        return;

    reset_votes();
    reset_mission();

    game.proposal = PROPOSAL_NONE;
    game.phase = PHASE_PROPOSAL;

    broadcast_status();

    char buffer[128];

    snprintf(buffer, sizeof(buffer), "\nCaptain %d is now Commodore.", game.commodore + 1);

    comm_broadcast(buffer);

    comm_request_action(game.commodore, "Choose a proposal:\n"
                                        "1. SAIL\n"
                                        "2. ANCHOR");
}

/* ---------- Voting ---------- */

static void begin_vote(void)
{
    reset_votes();
    game.phase = PHASE_VOTING;

    comm_broadcast("\nThe Commodore's proposal is now up for a vote.");

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        comm_request_action(i, "Vote:\n"
                               "1. APPROVE\n"
                               "2. REJECT");
    }
}

static void resolve_vote(void)
{
    int approve = 0;
    int reject = 0;

    char buffer[256];

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        if (game.players[i].vote_approve)
            approve++;
        else
            reject++;
    }

    snprintf(buffer, sizeof(buffer), "\nVote result: %d APPROVE / %d REJECT", approve, reject);

    comm_broadcast(buffer);

    /*
     * Four players means a 2-2 tie rejects the proposal.
     */
    if (approve <= reject)
    {
        game.rejected_proposals++;

        snprintf(buffer, sizeof(buffer), "Proposal rejected. Delay warning: %d/%d",
                 game.rejected_proposals, REJECTIONS_FOR_DELAY);

        comm_broadcast(buffer);

        if (game.rejected_proposals >= REJECTIONS_FOR_DELAY)
        {
            game.pirate_delay++;
            game.rejected_proposals = 0;

            comm_broadcast("Too much time has been wasted arguing.\n"
                           "The pursuing pirate fleet draws closer!");
        }

        next_commodore();
        begin_round();
        return;
    }

    /*
     * An accepted proposal resets the rejection counter.
     */
    game.rejected_proposals = 0;

    if (game.proposal == PROPOSAL_ANCHOR)
    {
        for (int i = 0; i < NUM_PLAYERS; i++)
        {
            game.players[i].supplies += ANCHOR_RECOVERY;

            if (game.players[i].supplies > MAX_SUPPLIES)
                game.players[i].supplies = MAX_SUPPLIES;
        }

        game.pirate_delay++;

        comm_broadcast("\nThe fleet drops anchor and replenishes supplies.\n"
                       "The delay allows the pirates to draw closer.");

        next_commodore();
        begin_round();
        return;
    }

    /*
     * Otherwise begin the sailing mission.
     */
    game.phase = PHASE_MISSION;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        if (!game.players[i].selected)
            continue;

        snprintf(buffer, sizeof(buffer),
                 "You have %d supplies.\n"
                 "How many supplies will you contribute?",
                 game.players[i].supplies);

        comm_request_action(i, buffer);
    }
}

/* ---------- Mission ---------- */

static void resolve_mission(void)
{
    int total = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        Player *p = &game.players[i];

        if (!p->selected)
            continue;

        total += p->contribution;
        p->supplies -= p->contribution;
    }

    /*
     * Do NOT reveal individual contributions.
     */
    if (total >= MISSION_REQUIREMENT)
    {
        game.convoy_progress++;

        comm_broadcast("\nMISSION SUCCESSFUL!\n"
                       "The convoy continues toward port.");
    }
    else
    {
        game.pirate_delay++;

        comm_broadcast("\nMISSION FAILED!\n"
                       "The convoy has been delayed.");
    }

    next_commodore();
    begin_round();
}

/* ---------- Action handling ---------- */

/*
 * In the real implementation I'd eventually replace atoi()-style
 * parsing with a small structured action protocol.
 */
static void handle_action(int player_id, const char *action)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS)
        return;

    Player *p = &game.players[player_id];

    /*
     * COMMODORE PROPOSAL
     */
    if (game.phase == PHASE_PROPOSAL)
    {
        if (player_id != game.commodore)
            return;

        if (strcmp(action, "1") == 0)
        {
            game.proposal = PROPOSAL_SAIL;
            game.phase = PHASE_CREW_SELECTION;

            /*
             * Temporary MVP:
             *
             * Ask the Commodore for a crew as:
             *
             *     1 2 4
             *
             * Later this should be its own structured request.
             */
            comm_request_action(player_id, "Enter 2 or 3 captain numbers for the mission "
                                           "(example: 1 2 4):");

            return;
        }

        if (strcmp(action, "2") == 0)
        {
            game.proposal = PROPOSAL_ANCHOR;

            comm_broadcast("\nThe Commodore proposes ANCHORING "
                           "to replenish supplies.");

            begin_vote();
            return;
        }

        comm_request_action(player_id, "Invalid proposal. Choose:\n"
                                       "1. SAIL\n"
                                       "2. ANCHOR");
        return;
    }

    /*
     * CREW SELECTION
     */
    if (game.phase == PHASE_CREW_SELECTION)
    {
        if (player_id != game.commodore)
            return;

        if (!parse_crew(action))
        {
            comm_request_action(player_id, "Invalid crew. Enter 2 or 3 distinct captain numbers "
                                           "(example: 1 2 4):");
            return;
        }

        comm_broadcast("\nThe Commodore proposes SAILING with the selected crew.");
        begin_vote();
        return;
    }

    /*
     * VOTE
     */
    if (game.phase == PHASE_VOTING)
    {
        if (p->voted)
            return;

        if (strcmp(action, "1") == 0)
            p->vote_approve = true;
        else if (strcmp(action, "2") == 0)
            p->vote_approve = false;
        else
        {
            comm_request_action(player_id, "Invalid vote. Choose:\n"
                                           "1. APPROVE\n"
                                           "2. REJECT");
            return;
        }

        p->voted = true;

        int votes = 0;

        for (int i = 0; i < NUM_PLAYERS; i++)
            if (game.players[i].voted)
                votes++;

        if (votes == NUM_PLAYERS)
            resolve_vote();

        return;
    }

    /*
     * MISSION CONTRIBUTION
     */
    if (game.phase == PHASE_MISSION)
    {
        if (!p->selected || p->contributed)
            return;

        char *end;
        long amount = strtol(action, &end, 10);

        if (*end != '\0' || amount < 0 || amount > p->supplies)
        {

            comm_request_action(player_id, "Invalid contribution. Try again.");

            return;
        }

        p->contribution = (int)amount;
        p->contributed = true;

        int finished = 0;

        for (int i = 0; i < NUM_PLAYERS; i++)
        {
            if (game.players[i].selected && game.players[i].contributed)
            {
                finished++;
            }
        }

        if (finished == game.crew_size)
            resolve_mission();
    }
}

/* ---------- Crew selection ---------- */

/*
 * This is separate because after choosing SAIL, the next response
 * from the Commodore represents a crew rather than another proposal.
 */
static bool parse_crew(const char *input)
{
    int ids[3];
    int count = 0;

    char copy[128];
    snprintf(copy, sizeof(copy), "%s", input);

    char *token = strtok(copy, " ");

    while (token != NULL && count < 3)
    {
        char *end;
        long n = strtol(token, &end, 10);

        if (*end != '\0' || n < 1 || n > NUM_PLAYERS)
            return false;

        ids[count++] = (int)n - 1;
        token = strtok(NULL, " ");
    }

    if (token != NULL)
        return false;

    if (count != 2 && count != 3)
        return false;

    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if (ids[i] == ids[j])
                return false;

    reset_mission();

    for (int i = 0; i < count; i++)
        game.players[ids[i]].selected = true;

    game.crew_size = count;

    return true;
}

/* ---------- Connections ---------- */

static void handle_connect(int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS)
        return;

    if (game.players[player_id].connected)
        return;

    game.players[player_id].connected = true;
    game.connected_players++;

    char buffer[128];

    snprintf(buffer, sizeof(buffer), "Captain %d has joined the fleet. (%d/%d)", player_id + 1,
             game.connected_players, NUM_PLAYERS);

    comm_broadcast(buffer);

    if (game.connected_players == NUM_PLAYERS)
    {
        comm_broadcast("\nAll captains are present.\n"
                       "The convoy is leaving harbor...");

        assign_roles();
        begin_round();
    }
}

/* ---------- Initialization ---------- */

static void init_game(void)
{
    memset(&game, 0, sizeof(game));

    game.phase = PHASE_WAITING;
    game.commodore = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
        game.players[i].id = i;
}

int main(void)
{
    srand((unsigned int)time(NULL));

    init_game();

    printf("Naval Convoy server starting...\n");

    /*
     * Exact callback signatures can change when we implement comm.c.
     */
    int r = comm_server_init(handle_connect, handle_action);

    if (r < 0)
    {
        fprintf(stderr, "Failed to initialize communications: %d\n", r);
        return EXIT_FAILURE;
    }

    printf("Waiting for %d captains...\n", NUM_PLAYERS);

    r = comm_server_run();

    comm_close();

    return (r < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
