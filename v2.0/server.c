#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "comm.h"

#define NUM_PLAYERS 4

#define STARTING_MANPOWER 6
#define MAX_MANPOWER 6
#define REST_RECOVERY 3

#define PROGRESS_TO_WIN 4
#define DELAY_TO_WIN 4
#define REJECTIONS_FOR_DELAY 3
#define MISSION_REQUIREMENT 6

#define DISCUSSION_SECONDS 30
#define PROPOSAL_SECONDS 30
#define VOTE_SECONDS 30
#define RESOURCE_SECONDS 30
#define RESULT_PAUSE_SECONDS 5
#define INTRO_PART_SECONDS 8
#define WARNING_SECONDS 10

typedef enum
{
    ROLE_NAVY,
    ROLE_PIRATE
} Role;

typedef enum
{
    PHASE_WAITING,
    PHASE_DISCUSSION,
    PHASE_PROPOSAL,
    PHASE_CREW_SELECTION,
    PHASE_VOTING,
    PHASE_MISSION,
    PHASE_RESULT,
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
    int manpower;
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

/* ========================================================= */
/* Forward declarations                                      */
/* ========================================================= */

static bool parse_crew(const char *input);

static void begin_round(void);
static void intro_part_two(void);
static void intro_part_three(void);

static void discussion_warning(void);
static void start_proposal(void);
static void proposal_warning(void);
static void crew_selection_warning(void);
static void proposal_timeout(void);

static void start_voting(void);
static void voting_warning(void);
static void voting_timeout(void);
static void resolve_vote(void);

static void resource_warning(void);
static void resource_timeout(void);
static void resolve_mission(void);

/* ========================================================= */
/* Utility                                                   */
/* ========================================================= */

static void set_everyone_mode(comm_input_mode mode) { (void)comm_broadcast_input_mode(mode); }

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

static void broadcast_status(void)
{
    /* Privately remind each captain of their role and manpower. */
    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        char buffer[256];

        if (game.players[i].role == ROLE_PIRATE)
        {
            snprintf(buffer, sizeof(buffer),
                     "\n---------------------------------\n"
                     "ROLE: PIRATE INFILTRATOR\n"
                     "Goal: Delay the convoy until your fleet catches it.\n"
                     "Manpower: %d/%d\n"
                     "---------------------------------",
                     game.players[i].manpower, MAX_MANPOWER);
        }
        else
        {
            snprintf(buffer, sizeof(buffer),
                     "\n---------------------------------\n"
                     "ROLE: ROYAL NAVY CAPTAIN\n"
                     "Goal: Get the convoy safely to port.\n"
                     "Manpower: %d/%d\n"
                     "---------------------------------",
                     game.players[i].manpower, MAX_MANPOWER);
        }

        (void)comm_send_message(i, buffer);
    }
}

/*
 * Leave an outcome on screen briefly before beginning
 * the next round.
 */
static void pause_before_next_round(void)
{
    game.phase = PHASE_RESULT;

    set_everyone_mode(COMM_INPUT_NONE);

    next_commodore();

    if (comm_set_timer(RESULT_PAUSE_SECONDS, begin_round) < 0)
    {

        begin_round();
    }
}

/* ========================================================= */
/* Victory                                                   */
/* ========================================================= */

static bool check_victory(void)
{
    if (game.convoy_progress >= PROGRESS_TO_WIN)
    {

        game.phase = PHASE_GAME_OVER;

        set_everyone_mode(COMM_INPUT_NONE);

        (void)comm_broadcast("\n=================================\n"
                             "        THE CONVOY IS SAFE\n"
                             "=================================\n"
                             "The fleet reaches port at last.\n\n"
                             "ROYAL NAVY VICTORY!");

        return true;
    }

    if (game.pirate_delay >= DELAY_TO_WIN)
    {

        game.phase = PHASE_GAME_OVER;

        set_everyone_mode(COMM_INPUT_NONE);

        (void)comm_broadcast("\n=================================\n"
                             "        BLACK SAILS AHEAD\n"
                             "=================================\n"
                             "The convoy has been delayed too long.\n"
                             "The pursuing pirate fleet closes in.\n\n"
                             "PIRATE VICTORY!");

        return true;
    }

    return false;
}

/* ========================================================= */
/* Roles                                                     */
/* ========================================================= */

static void assign_roles(void)
{
    int pirate = rand() % NUM_PLAYERS;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        Player *player = &game.players[i];

        player->role = (i == pirate) ? ROLE_PIRATE : ROLE_NAVY;

        player->manpower = STARTING_MANPOWER;

        if (player->role == ROLE_PIRATE)
        {

            (void)comm_send_message(player->id, "\nYour orders have arrived...\n\n"
                                                "You are the PIRATE INFILTRATOR.\n"
                                                "Delay the convoy until your fleet catches it.");
        }
        else
        {

            (void)comm_send_message(player->id, "\nYour orders have arrived...\n\n"
                                                "You are a ROYAL NAVY CAPTAIN.\n"
                                                "Get the convoy safely to port.");
        }
    }
}

/* ========================================================= */
/* Round                                                     */
/* ========================================================= */

static void begin_round(void)
{
    if (check_victory())
        return;

    reset_votes();
    reset_mission();

    game.proposal = PROPOSAL_NONE;
    game.phase = PHASE_DISCUSSION;

    broadcast_status();

    /* Privately tell each captain their current manpower. */
    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        char manpower_buffer[128];

        snprintf(manpower_buffer, sizeof(manpower_buffer), "\nYou currently have %d/%d manpower.",
                 game.players[i].manpower, MAX_MANPOWER);

        (void)comm_send_message(i, manpower_buffer);
    }

    char buffer[256];

    snprintf(buffer, sizeof(buffer),
             "\nCaptain %d now holds command as Commodore.\n"
             "The fleet has %d seconds to discuss its next move.",
             game.commodore + 1, DISCUSSION_SECONDS);

    (void)comm_broadcast(buffer);

    set_everyone_mode(COMM_INPUT_CHAT);

    if (comm_set_timer(DISCUSSION_SECONDS - WARNING_SECONDS, discussion_warning) < 0)
    {
        discussion_warning();
    }
}

static void discussion_warning(void)
{
    if (game.phase != PHASE_DISCUSSION)
        return;

    (void)comm_broadcast("\n*** 10 seconds remaining to discuss. ***");

    if (comm_set_timer(WARNING_SECONDS, start_proposal) < 0)
        start_proposal();
}

/* ========================================================= */
/* Proposal                                                  */
/* ========================================================= */

static void start_proposal(void)
{
    if (game.phase != PHASE_DISCUSSION)
        return;

    game.phase = PHASE_PROPOSAL;

    set_everyone_mode(COMM_INPUT_NONE);

    (void)comm_broadcast("\nThe discussion falls silent.\n"
                         "The Commodore must decide.");

    char prompt[192];

    snprintf(prompt, sizeof(prompt),
             "\nChoose the fleet's course:\n"
             "1. SAIL\n"
             "2. ANCHOR\n\n"
             "You have %d seconds.",
             PROPOSAL_SECONDS);

    (void)comm_request_action(game.commodore, prompt);

    if (comm_set_timer(PROPOSAL_SECONDS - WARNING_SECONDS, proposal_warning) < 0)
    {
        proposal_warning();
    }
}

static void proposal_warning(void)
{
    if (game.phase != PHASE_PROPOSAL)
        return;

    (void)comm_send_message(game.commodore,
                            "\n*** 10 seconds remaining to choose SAIL or ANCHOR. ***");

    if (comm_set_timer(WARNING_SECONDS, proposal_timeout) < 0)
        proposal_timeout();
}

static void crew_selection_warning(void)
{
    if (game.phase != PHASE_CREW_SELECTION)
        return;

    (void)comm_send_message(game.commodore,
                            "\n*** 10 seconds remaining to select a crew. ***");

    if (comm_set_timer(WARNING_SECONDS, proposal_timeout) < 0)
        proposal_timeout();
}

static void proposal_timeout(void)
{
    if (game.phase != PHASE_PROPOSAL && game.phase != PHASE_CREW_SELECTION)
    {

        return;
    }

    (void)comm_set_input_mode(game.commodore, COMM_INPUT_NONE);

    game.rejected_proposals++;

    char buffer[256];

    snprintf(buffer, sizeof(buffer),
             "\nThe Commodore hesitates too long.\n"
             "The opportunity is lost.\n\n"
             "Lost turns: %d/%d",
             game.rejected_proposals, REJECTIONS_FOR_DELAY);

    (void)comm_broadcast(buffer);

    if (game.rejected_proposals >= REJECTIONS_FOR_DELAY)
    {

        game.rejected_proposals = 0;
        game.pirate_delay++;

        (void)comm_broadcast("\nToo much time has been lost.\n"
                             "Black sails creep closer on the horizon...");
    }

    pause_before_next_round();
}

/* ========================================================= */
/* Voting                                                    */
/* ========================================================= */

static void start_voting(void)
{
    /*
     * Remove the proposal timer before installing the
     * voting timer.
     */
    (void)comm_cancel_timer();

    reset_votes();

    game.phase = PHASE_VOTING;

    set_everyone_mode(COMM_INPUT_NONE);

    (void)comm_broadcast("\nThe proposal now goes before the fleet.");

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        char prompt[128];

        snprintf(prompt, sizeof(prompt),
                 "\nCast your vote:\n"
                 "1. APPROVE\n"
                 "2. REJECT\n\n"
                 "You have %d seconds.",
                 VOTE_SECONDS);

        (void)comm_request_action(i, prompt);
    }

    if (comm_set_timer(VOTE_SECONDS - WARNING_SECONDS, voting_warning) < 0)
    {
        voting_warning();
    }
}

static void voting_warning(void)
{
    if (game.phase != PHASE_VOTING)
        return;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        if (!game.players[i].voted)
            (void)comm_send_message(i, "\n*** 10 seconds remaining to vote. ***");
    }

    if (comm_set_timer(WARNING_SECONDS, voting_timeout) < 0)
        voting_timeout();
}

static void voting_timeout(void)
{
    if (game.phase != PHASE_VOTING)
        return;

    (void)comm_broadcast("\nVoting time has expired.\n"
                         "Captains who failed to vote are counted as REJECT.");

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        Player *player = &game.players[i];

        if (player->voted)
            continue;

        player->voted = true;
        player->vote_approve = false;

        (void)comm_set_input_mode(i, COMM_INPUT_NONE);
    }

    resolve_vote();
}

static void resolve_vote(void)
{
    int approve = 0;
    int reject = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        if (game.players[i].vote_approve)
            approve++;
        else
            reject++;
    }

    char buffer[256];

    snprintf(buffer, sizeof(buffer),
             "\nThe votes are counted...\n"
             "%d APPROVE / %d REJECT",
             approve, reject);

    (void)comm_broadcast(buffer);

    /*
     * Tie counts as rejection.
     */
    if (approve <= reject)
    {

        game.rejected_proposals++;

        snprintf(buffer, sizeof(buffer),
                 "\nThe proposal is REJECTED.\n"
                 "The fleet remains where it is.\n\n"
                 "Lost turns: %d/%d",
                 game.rejected_proposals, REJECTIONS_FOR_DELAY);

        (void)comm_broadcast(buffer);

        if (game.rejected_proposals >= REJECTIONS_FOR_DELAY)
        {

            game.rejected_proposals = 0;
            game.pirate_delay++;

            (void)comm_broadcast("\nToo much time has been lost.\n"
                                 "The pursuing pirates draw closer...");
        }

        pause_before_next_round();
        return;
    }

    /*
     * An approved proposal resets the rejection counter.
     */
    game.rejected_proposals = 0;

    (void)comm_broadcast("\nThe proposal is APPROVED.");

    /*
     * -----------------------------------------------------
     * ANCHOR
     * -----------------------------------------------------
     */
    if (game.proposal == PROPOSAL_ANCHOR)
    {

        for (int i = 0; i < NUM_PLAYERS; i++)
        {

            game.players[i].manpower += REST_RECOVERY;

            if (game.players[i].manpower > MAX_MANPOWER)
            {

                game.players[i].manpower = MAX_MANPOWER;
            }
        }

        game.pirate_delay++;

        (void)comm_broadcast("\nThe anchors plunge into the water.\n"
                             "The exhausted crews are given time to rest.\n\n"
                             "Manpower has been restored.\n"
                             "But the rest gives the pirates time to gain ground...");

        pause_before_next_round();
        return;
    }

    /*
     * -----------------------------------------------------
     * SAIL
     * -----------------------------------------------------
     */

    game.phase = PHASE_MISSION;

    set_everyone_mode(COMM_INPUT_NONE);

    (void)comm_broadcast("\nThe selected captains prepare their ships.\n"
                         "The mission begins.");

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        if (!game.players[i].selected)
            continue;

        snprintf(buffer, sizeof(buffer),
                 "\nYou have %d manpower available.\n"
                 "How much manpower will you commit?\n\n"
                 "You have %d seconds.",
                 game.players[i].manpower, RESOURCE_SECONDS);

        (void)comm_request_action(i, buffer);
    }

    /*
     * Voting has finished, so install the resource timer.
     *
     * If resolve_vote() was called by voting_timeout(),
     * the old timer has already fired and its callback
     * has been cleared.
     *
     * If everyone voted normally, handle_action() cancels
     * the voting timer before calling resolve_vote().
     */
    if (comm_set_timer(RESOURCE_SECONDS - WARNING_SECONDS, resource_warning) < 0)
    {
        resource_warning();
    }
}

static void resource_warning(void)
{
    if (game.phase != PHASE_MISSION)
        return;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {
        if (game.players[i].selected && !game.players[i].contributed)
            (void)comm_send_message(i,
                                    "\n*** 10 seconds remaining to commit manpower. ***");
    }

    if (comm_set_timer(WARNING_SECONDS, resource_timeout) < 0)
        resource_timeout();
}

/* ========================================================= */
/* Manpower timeout                                          */
/* ========================================================= */

static void resource_timeout(void)
{
    if (game.phase != PHASE_MISSION)
        return;

    (void)comm_broadcast("\nMission preparations are over.\n"
                         "Any captain who failed to commit manpower contributes 0.");

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        Player *player = &game.players[i];

        if (!player->selected || player->contributed)
        {

            continue;
        }

        player->contribution = 0;
        player->contributed = true;

        (void)comm_set_input_mode(i, COMM_INPUT_NONE);
    }

    resolve_mission();
}

/* ========================================================= */
/* Mission resolution                                        */
/* ========================================================= */

static void resolve_mission(void)
{
    int total = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        Player *player = &game.players[i];

        if (!player->selected)
            continue;

        total += player->contribution;

        player->manpower -= player->contribution;
    }

    set_everyone_mode(COMM_INPUT_NONE);

    if (total >= MISSION_REQUIREMENT)
    {

        game.convoy_progress++;

        (void)comm_broadcast("\n=================================\n"
                             "        MISSION SUCCESSFUL\n"
                             "=================================\n"
                             "The convoy presses onward toward port.");
    }
    else
    {

        game.pirate_delay++;

        (void)comm_broadcast("\n=================================\n"
                             "          MISSION FAILED\n"
                             "=================================\n"
                             "The convoy loses precious time.\n"
                             "The pirates gain ground...");
    }

    /*
     * Don't immediately bury the result under the
     * next round's messages.
     */
    pause_before_next_round();
}

/* ========================================================= */
/* Crew parsing                                              */
/* ========================================================= */

static bool parse_crew(const char *input)
{
    int ids[3];
    int count = 0;

    char copy[128];

    snprintf(copy, sizeof(copy), "%s", input);

    char *token = strtok(copy, " ");

    while (token != NULL && count < 3)
    {

        char *end = NULL;

        long number = strtol(token, &end, 10);

        if (end == token || *end != '\0' || number < 1 || number > NUM_PLAYERS)
        {

            return false;
        }

        ids[count++] = (int)number - 1;

        token = strtok(NULL, " ");
    }

    /*
     * More than three numbers.
     */
    if (token != NULL)
        return false;

    if (count != 2 && count != 3)
    {

        return false;
    }

    /*
     * Reject duplicate captains.
     */
    for (int i = 0; i < count; i++)
    {

        for (int j = i + 1; j < count; j++)
        {

            if (ids[i] == ids[j])
                return false;
        }
    }

    reset_mission();

    for (int i = 0; i < count; i++)
    {

        game.players[ids[i]].selected = true;
    }

    game.crew_size = count;

    return true;
}

/* ========================================================= */
/* Actions                                                   */
/* ========================================================= */

static void handle_action(int player_id, const char *action)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || action == NULL)
    {

        return;
    }

    Player *player = &game.players[player_id];

    /*
     * -----------------------------------------------------
     * Commodore chooses SAIL or ANCHOR
     * -----------------------------------------------------
     */
    if (game.phase == PHASE_PROPOSAL)
    {

        if (player_id != game.commodore)
            return;

        /*
         * SAIL
         */
        if (strcmp(action, "1") == 0)
        {

            game.proposal = PROPOSAL_SAIL;
            game.phase = PHASE_CREW_SELECTION;

            /* SAIL gets a fresh 30-second crew-selection period. */
            (void)comm_cancel_timer();

            (void)comm_request_action(player_id,
                                      "\nChoose 2 or 3 captains for the mission.\n"
                                      "Example: 1 2 4\n\n"
                                      "You have 30 seconds.");

            if (comm_set_timer(PROPOSAL_SECONDS - WARNING_SECONDS,
                               crew_selection_warning) < 0)
            {
                crew_selection_warning();
            }

            return;
        }

        /*
         * ANCHOR
         */
        if (strcmp(action, "2") == 0)
        {

            game.proposal = PROPOSAL_ANCHOR;

            (void)comm_set_input_mode(player_id, COMM_INPUT_NONE);

            char buffer[192];

            snprintf(buffer, sizeof(buffer),
                     "\nCommodore Captain %d gives the order:\n"
                     "ANCHOR THE FLEET.",
                     game.commodore + 1);

            (void)comm_broadcast(buffer);

            start_voting();

            return;
        }

        (void)comm_request_action(player_id, "Invalid proposal.\n"
                                             "1. SAIL\n"
                                             "2. ANCHOR");

        return;
    }

    /*
     * -----------------------------------------------------
     * Crew selection
     * -----------------------------------------------------
     */
    if (game.phase == PHASE_CREW_SELECTION)
    {

        if (player_id != game.commodore)
            return;

        if (!parse_crew(action))
        {

            (void)comm_request_action(player_id, "Invalid crew.\n"
                                                 "Enter 2 or 3 distinct captain numbers.\n"
                                                 "Example: 1 2 4");

            return;
        }

        (void)comm_set_input_mode(player_id, COMM_INPUT_NONE);

        char buffer[256];

        int offset =
            snprintf(buffer, sizeof(buffer), "\nCommodore Captain %d selects ", game.commodore + 1);

        int listed = 0;

        for (int i = 0; i < NUM_PLAYERS; i++)
        {

            if (!game.players[i].selected)
                continue;

            if (listed > 0)
            {

                offset += snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, ", ");
            }

            offset +=
                snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, "Captain %d", i + 1);

            listed++;
        }

        snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, " for the mission.");

        (void)comm_broadcast(buffer);

        start_voting();

        return;
    }

    /*
     * -----------------------------------------------------
     * Voting
     * -----------------------------------------------------
     */
    if (game.phase == PHASE_VOTING)
    {

        if (player->voted)
            return;

        if (strcmp(action, "1") == 0)
        {

            player->vote_approve = true;
        }
        else if (strcmp(action, "2") == 0)
        {

            player->vote_approve = false;
        }
        else
        {

            (void)comm_request_action(player_id, "Invalid vote.\n"
                                                 "1. APPROVE\n"
                                                 "2. REJECT");

            return;
        }

        player->voted = true;

        (void)comm_set_input_mode(player_id, COMM_INPUT_NONE);

        int votes = 0;

        for (int i = 0; i < NUM_PLAYERS; i++)
        {

            if (game.players[i].voted)
                votes++;
        }

        /*
         * Everyone voted before the timer expired.
         */
        if (votes == NUM_PLAYERS)
        {

            (void)comm_cancel_timer();

            resolve_vote();
        }

        return;
    }

    /*
     * -----------------------------------------------------
     * Manpower contribution
     * -----------------------------------------------------
     */
    if (game.phase == PHASE_MISSION)
    {

        if (!player->selected || player->contributed)
        {

            return;
        }

        char *end = NULL;

        long amount = strtol(action, &end, 10);

        if (end == action || *end != '\0' || amount < 0 || amount > player->manpower)
        {

            (void)comm_request_action(player_id, "Invalid contribution.\n"
                                                 "Enter a number between 0 and "
                                                 "your available manpower.");

            return;
        }

        player->contribution = (int)amount;

        player->contributed = true;

        (void)comm_set_input_mode(player_id, COMM_INPUT_NONE);

        int finished = 0;

        for (int i = 0; i < NUM_PLAYERS; i++)
        {

            if (game.players[i].selected && game.players[i].contributed)
            {

                finished++;
            }
        }

        /*
         * Everyone selected for the mission has
         * contributed before the timer expired.
         */
        if (finished == game.crew_size)
        {

            (void)comm_cancel_timer();

            resolve_mission();
        }

        return;
    }
}

/* ========================================================= */
/* Chat                                                      */
/* ========================================================= */

static void handle_chat(int player_id, const char *message)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS || message == NULL || message[0] == '\0')
    {

        return;
    }

    /*
     * Chat only exists during the discussion period.
     */
    if (game.phase != PHASE_DISCUSSION)
        return;

    char buffer[384];

    snprintf(buffer, sizeof(buffer), "Captain %d: %s", player_id + 1, message);

    (void)comm_broadcast(buffer);
}

/* ========================================================= */
/* Introduction                                              */
/* ========================================================= */

static void intro_part_two(void)
{
    (void)comm_broadcast("\n=================================\n"
                         "        HOW A ROUND WORKS\n"
                         "=================================\n\n"
                         "One Captain becomes Commodore and proposes an action.\n\n"
                         "SAIL\n"
                         "  The Commodore selects 2 or 3 Captains for a mission.\n"
                         "  Selected Captains secretly commit manpower.\n"
                         "  The mission needs 6 total manpower to succeed.\n"
                         "  Success advances the convoy; failure advances the pirates.\n\n"
                         "ANCHOR\n"
                         "  The fleet stops so exhausted crews can rest.\n"
                         "  Every Captain recovers 3 manpower, up to a maximum of 6.\n"
                         "  But the delay also advances the pirates.\n\n"
                         "More instructions will follow...");

    if (comm_set_timer(INTRO_PART_SECONDS, intro_part_three) < 0)
        intro_part_three();
}

static void intro_part_three(void)
{
    (void)comm_broadcast("\n=================================\n"
                         "       VOTING AND STRATEGY\n"
                         "=================================\n\n"
                         "Every proposal must be approved by the fleet.\n"
                         "A tie is a rejection. Three rejected or missed proposals\n"
                         "also advance the pirates.\n\n"
                         "Manpower is limited. Watch who contributes, who pushes to\n"
                         "anchor, and which missions mysteriously fail.\n\n"
                         "Discuss, accuse, bluff, and cooperate.\n"
                         "One of you is working against the fleet.\n\n"
                         "Trust carefully.\n\n"
                         "Your secret orders are arriving now...\n"
                         "The game begins in 8 seconds.\n"
                         "=================================");

    assign_roles();

    if (comm_set_timer(INTRO_PART_SECONDS, begin_round) < 0)
        begin_round();
}

/* ========================================================= */
/* Connections                                               */
/* ========================================================= */

static void handle_connect(int player_id)
{
    if (player_id < 0 || player_id >= NUM_PLAYERS)
    {

        return;
    }

    if (game.players[player_id].connected)
        return;

    game.players[player_id].connected = true;

    game.connected_players++;

    (void)comm_set_input_mode(player_id, COMM_INPUT_NONE);

    char buffer[128];

    snprintf(buffer, sizeof(buffer), "Captain %d has joined the fleet. (%d/%d)", player_id + 1,
             game.connected_players, NUM_PLAYERS);

    (void)comm_broadcast(buffer);

    if (game.connected_players == NUM_PLAYERS)
    {
        set_everyone_mode(COMM_INPUT_NONE);

        (void)comm_broadcast("\n=================================\n"
                             "          CONVOY CHASE\n"
                             "=================================\n\n"
                             "A Royal Navy convoy carrying valuable cargo is racing\n"
                             "toward port. But one Captain is secretly a PIRATE\n"
                             "INFILTRATOR, working to delay the fleet until pursuing\n"
                             "pirates can catch it.\n\n"
                             "HOW TO WIN\n"
                             "----------\n"
                             "ROYAL NAVY: Advance the convoy 4 times and reach port.\n"
                             "PIRATE:     Cause 4 delays before the convoy reaches port.\n\n"
                             "Read carefully. More instructions will follow...");

        if (comm_set_timer(INTRO_PART_SECONDS, intro_part_two) < 0)
            intro_part_two();
    }
}

/* ========================================================= */
/* Initialization                                            */
/* ========================================================= */

static void init_game(void)
{
    memset(&game, 0, sizeof(game));

    game.phase = PHASE_WAITING;

    game.commodore = 0;

    for (int i = 0; i < NUM_PLAYERS; i++)
    {

        game.players[i].id = i;
    }
}

/* ========================================================= */
/* Main                                                      */
/* ========================================================= */

int main(void)
{
    srand((unsigned int)time(NULL));

    init_game();

    printf("Convoy Chase server starting...\n");

    int r = comm_server_init(handle_connect, handle_action, handle_chat);

    if (r < 0)
    {

        fprintf(stderr, "Failed to initialize communications: %d\n", r);

        comm_close();

        return EXIT_FAILURE;
    }

    printf("Waiting for %d captains...\n", NUM_PLAYERS);

    r = comm_server_run();

    comm_close();

    return (r < 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
