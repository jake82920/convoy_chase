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

All communication between `server.c` and `player.c` is performed through the interface defined in `comm.h`.

The distributed
