# Convoy Chase

Convoy Chase is a four-player social deduction game for Linux.

Three players are Royal Navy Captains trying to escort a valuable convoy safely to port. One player is secretly a Pirate Infiltrator trying to delay the convoy until pursuing pirates can catch it.

This version runs locally on one Linux machine. The server and each player run as separate Linux users and communicate through the D-Bus system bus.

## Installation

These instructions are intended for Ubuntu 26.04 LTS.

Install GNU Make:

```bash
sudo apt install make
```

Clone the repository:

```bash
git clone https://github.com/jake82920/convoy_chase.git
```

From the directory containing v1.1, run:

```bash
sudo make install
```

The installation creates the dedicated server and player users, installs the game, and configures the D-Bus policy required for communication.

Set passwords for the users if needed:

```bash
sudo passwd <username>
```

## Running the Game

Convoy Chase is a terminal-based game. Each player should connect to a terminal as their assigned Linux user.

For example, terminals can be opened using SSH or by switching users:

```bash
su - <username>
```

The installation script also enables the SSH server.

As the `server` user, run:

```bash
./server
```

The server will wait for all four players to connect.

As each player user, run the corresponding Captain:

```bash
./player <1-4>
```

For example:

```bash
./player 1
```

Once all four Captains have connected, the game begins automatically.

## How to Play

A Royal Navy convoy carrying valuable cargo is racing toward port.

One Captain is secretly a **PIRATE INFILTRATOR**, working to delay the fleet until pursuing pirates can catch it.

### Royal Navy

Advance the convoy **4 times** to reach port.

### Pirate

Cause **4 delays** before the convoy reaches port.

## Rounds

At the beginning of each round, one Captain becomes the **Commodore**.

The fleet has time to discuss its next move. During this time, players can talk, accuse, bluff, and plan.

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
Makefile    Build and installation
README.md   Game and project documentation
```

## Communication

The communication library is implemented in **`comm.c`**, with its public interface declared in **`comm.h`**. All communication between `server.c` and `player.c` goes through this library, keeping the D-Bus implementation out of the game logic.

The library uses **libsystemd's sd-bus API** to communicate through the D-Bus system bus. The server owns the well-known D-Bus name:

```text
edu.asu.Convoy.Server
```

The server exposes D-Bus methods used for player registration, actions, and chat. Each player also exposes methods that allow the server to send private messages, request actions, and control when that player may provide input.

The game uses dedicated Linux users for the server and each player. A D-Bus system-bus policy controls which users may communicate with the game and restricts ownership of the server's well-known bus name to the server user.

The server also uses D-Bus sender credentials to authenticate players. When a player registers, the communication library verifies the player's Linux UID and records the player's unique D-Bus sender name. Subsequent actions and chat messages are accepted only when they match the registered identity.

Most game communication uses D-Bus method calls. Private information can therefore be sent to an individual player, while broadcasts are implemented by sending a message individually to each connected player.

Calls that do not require an immediate result are issued asynchronously. D-Bus activity, timers, and terminal input are integrated into a single `poll()`-based event loop so the game can respond to multiple players without creating a thread for each connection.

## Communication API

The public communication interface is declared in **`comm.h`**.

The server uses functions including:

- `comm_send_message()` - sends a private message to one player.
- `comm_broadcast()` - sends a message to all connected players.
- `comm_request_action()` - requests a game action from a player.

The communication library also provides the infrastructure for:

- Server and player initialization
- Player registration
- Player actions
- Player chat
- Private and broadcast messages
- Input-mode control
- Timers
- D-Bus event handling
- Cleanup

These operations are implemented inside `comm.c`, allowing `server.c` and `player.c` to interact through the communication API rather than directly using sd-bus.

## Terminal Input

The player interface uses **GNU Readline** for interactive terminal input.

Readline is integrated with the communication event loop so that incoming messages can be displayed while a player is typing without discarding the player's current input. This allows chat, server messages, timers, and D-Bus events to coexist within the single-threaded event-driven design.
