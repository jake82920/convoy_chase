# Convoy Chase

Convoy Chase is a four-player social deduction game for Linux.

Three players are Royal Navy Captains trying to escort a valuable convoy safely to port. One player is secretly a Pirate Infiltrator trying to delay the convoy until pursuing pirates can catch it.

This version is a distributed system. The game runs on one authoritative server, and four player clients connect to it over TCP/IP.

## Installation

Clone the repository onto the server and each player machine.

On the **server**:

```bash
sudo make install-server
./server
```

On each **player machine**:

```bash
sudo make install-client
./player <captain 1-4> <server-address>
```

For example:

```bash
./player 1 192.168.122.100
```

Each player chooses a different Captain number from 1 through 4.

Once all four Captains are connected, the game begins automatically.

## How to Play

A Royal Navy convoy carrying valuable cargo is racing toward port.

One Captain is secretly a **PIRATE INFILTRATOR**, working to delay the fleet until pursuing pirates can catch it.

### Royal Navy

Advance the convoy **4 times** to reach port.

### Pirate

Cause **4 delays** before the convoy reaches port.

## Rounds

At the beginning of each round, one Captain becomes the **Commodore**.

The fleet has 30 seconds to discuss its next move. During this time, players can talk, accuse, bluff, and plan.

The Commodore then chooses between **SAIL** and **ANCHOR**.

### SAIL

The Commodore selects 2 or 3 Captains for the mission.

The fleet votes on the proposed crew. If the proposal is approved, each selected Captain secretly chooses how much manpower to contribute.

A mission requires a combined **6 manpower** to succeed.

A successful mission advances the convoy.

A failed mission advances the pirates.

### ANCHOR

The Commodore proposes stopping the fleet to rest.

If the fleet approves, every Captain recovers **3 manpower**, up to the maximum of 6.

However, stopping also gives the pursuing pirates time to advance.

## Voting

Every SAIL or ANCHOR proposal must be approved by the fleet.

Each Captain votes to:

```text
1. APPROVE
2. REJECT
```

A tie counts as a rejection.

Three rejected or missed proposals cause another delay and advance the pirates.

An approved proposal resets the rejection counter.

## Manpower

Every Captain begins with **6 manpower**.

Captains selected for a mission secretly choose how much of their available manpower to contribute.

Committed manpower is spent when the mission is resolved.

Because only the combined result is revealed, the fleet does not know which Captain caused a mission to fail.

Managing manpower is therefore important. Spending too much early can leave loyal Captains exhausted, but conserving too much can cause a mission to fail.

Anchoring restores manpower, but every anchor also brings the pirates closer.

## Strategy

Watch the other Captains carefully.

Who keeps proposing an anchor?

Who supports suspicious crews?

Why did a mission fail?

Is someone conserving manpower, or deliberately sabotaging the convoy?

The Pirate has the same actions as every other Captain. They must manipulate missions, votes, and the fleet's limited manpower without revealing themselves.

Discuss, accuse, bluff, and cooperate.

**Trust carefully.**

## Project Files

```text
server.c    Authoritative game state and game rules
player.c    Player terminal interface
comm.c      Communication implementation
comm.h      Communication interface
Makefile    Build and dependency installation
README.md   Game and project documentation
```

## Communication

The communication library is implemented in **`comm.c`**, with its public interface declared in **`comm.h`**. All communication between `server.c` and `player.c` goes through this library. The game code does not access sockets directly.

The distributed implementation uses TCP/IP on port **8292**. The server is authoritative: player clients send actions and chat messages to the server, and the server sends game messages, prompts, and input-mode changes back to the clients. `comm.c` uses an application-level framing protocol so messages can be reconstructed correctly from the TCP byte stream.

### Communication API

Player-side functions:

- `comm_init()` - connects a player to the server and registers the player's message, action-request, and input-mode callbacks.
- `comm_run()` - runs the player communication event loop.
- `comm_send_action()` - sends a requested game action to the server.
- `comm_send_chat()` - sends a discussion message to the server.

Server-side functions:

- `comm_server_init()` - creates the TCP server and registers callbacks for player connections, actions, and chat messages.
- `comm_server_run()` - runs the server communication event loop.
- `comm_send_message()` - sends a message to one player.
- `comm_request_action()` - prompts one player for a game action.
- `comm_set_input_mode()` - changes whether one player may enter chat, an action, or no input.
- `comm_broadcast_input_mode()` - changes the input mode for all players.
- `comm_broadcast()` - sends a message to all connected players.
- `comm_set_timer()` - starts a one-shot server timer with a callback.
- `comm_cancel_timer()` - cancels the active server timer.

Shared cleanup:

- `comm_close()` - releases sockets, timers, and terminal resources.

The library also handles player registration, nonblocking socket I/O, message framing, connection cleanup, and multiplexing of simultaneous clients. This keeps all inter-user and network communication contained in the communication library while `server.c` remains responsible for game state and rules and `player.c` remains responsible for the terminal interface.
