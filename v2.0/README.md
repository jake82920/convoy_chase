# Convoy Chase - Distributed TCP Version

This version replaces the Project 1 system D-Bus transport with a custom TCP/IP protocol. The game logic in `server.c` remains unchanged. The same repository is cloned onto every VM, and each VM installs only the role it needs.

## Architecture

- `server.c`: authoritative game state and rules
- `player.c`: terminal user interface
- `comm.h`: communication abstraction used by the game
- `comm.c`: raw POSIX TCP transport, message framing, `poll()` event loops, and server timers
- `Makefile`: builds and installs either the server role, client role, or both

The server listens on TCP port **8292**.

## Repository Layout

Every machine uses the same repository. You do not need separate server and client repositories.

```text
Convoy-Chase/
|-- server.c
|-- player.c
|-- comm.c
|-- comm.h
|-- Makefile
`-- README_DISTRIBUTED.md
```

## Server VM Installation

Clone the repository on the VM that will host the authoritative game server. From the repository directory, run:

```bash
sudo make install-server
```

This installs the required build dependencies, builds the server, and installs it as:

```text
/usr/local/bin/convoy-server
```

Start the server with:

```bash
convoy-server
```

The server listens for player connections on TCP port 8292.

## Client VM Installation

Clone the same repository on every player VM. From the repository directory, run:

```bash
sudo make install-client
```

This installs the required build dependencies, builds the player client, and installs it as:

```text
/usr/local/bin/convoy-player
```

A player connects using:

```bash
convoy-player <captain-number> <server-address>
```

For example, if the server VM is `192.168.56.10`:

```bash
convoy-player 1 192.168.56.10
```

The other clients would use:

```bash
convoy-player 2 192.168.56.10
convoy-player 3 192.168.56.10
convoy-player 4 192.168.56.10
```

Each captain number may have only one active TCP connection.

## Local Development Installation

To install both the server and client on one machine for localhost development:

```bash
sudo make install
```

Then run:

```bash
convoy-server
```

In four other terminals:

```bash
convoy-player 1 127.0.0.1
convoy-player 2 127.0.0.1
convoy-player 3 127.0.0.1
convoy-player 4 127.0.0.1
```

## Build Without Installing

To build both programs in the repository:

```bash
make
```

To build only the server:

```bash
make server
```

To build only the client:

```bash
make client
```

The current shared `comm.c` contains the Readline-based client event-loop support, so both builds link against Readline even though the server does not directly use terminal input.

## Network Setup

All client VMs must be able to reach the server VM over TCP port 8292.

Find the server VM's IP address with:

```bash
ip addr
```

From a client VM, basic connectivity can be checked with:

```bash
ping <server-ip>
```

If UFW is enabled on the server VM, allow the game port:

```bash
sudo ufw allow 8292/tcp
```

The client VMs do not need to accept incoming game connections. They initiate outbound TCP connections to the server.

## Recommended VM Topology

```text
                 TCP/IP

Player 1 VM ---------\
Player 2 VM ----------\
Player 3 VM -----------+--> Server VM : 8292
Player 4 VM ----------/
```

Only the server VM runs `convoy-server`. Each player VM runs one `convoy-player` process with its assigned captain number.

## TCP Protocol

TCP is a byte stream, so `comm.c` implements explicit application-level message framing. Each protocol message begins with two 32-bit unsigned integers in network byte order:

1. message type
2. payload length

The payload immediately follows the header. The receiver retains partial headers and payloads until a complete application message has arrived.

Protocol message types represent registration, ordinary server messages, action requests, input-mode changes, player actions, and chat messages.

## Testing Sequence

Before using five VMs, test the transport incrementally.

First, run the server and four clients on one VM using `127.0.0.1`. This verifies the game and TCP protocol without introducing VM networking problems.

Next, run the server on one VM and one player on a second VM. This verifies that the server is reachable across the VM network.

Finally, run the complete distributed system:

```text
Server VM
|-- TCP connection: Player 1 VM
|-- TCP connection: Player 2 VM
|-- TCP connection: Player 3 VM
`-- TCP connection: Player 4 VM
```

Play a complete game and verify discussion, proposals, crew selection, voting, manpower commitment, timeouts, warnings, and victory conditions.

## Uninstall

On a server VM:

```bash
sudo make uninstall-server
```

On a client VM:

```bash
sudo make uninstall-client
```

On a development machine where both were installed:

```bash
sudo make uninstall
```

These targets remove the installed Convoy Chase binaries. They do not remove general development packages such as GCC or Readline because those packages may be used by other software.

## Project 1 vs. Projects 2/3

Project 1 used the system D-Bus as an advanced local IPC mechanism between Linux processes. The distributed version refactors the communication subsystem to use raw POSIX TCP sockets. The application-facing `comm.h` abstraction is retained, allowing the game logic to remain essentially unchanged while the transport changes from local IPC to communication across independent machines.

The distributed implementation uses `socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`, `poll()`, and `timerfd` directly rather than an RPC or networking framework.

The resulting deployment model is intentionally simple: clone one repository everywhere, install the server role on the server VM, and install the client role on each player VM.

## Current Scope

The server rejects invalid captain numbers and duplicate simultaneous captain connections. This version does not provide cryptographic authentication or automatic restoration of game state after a disconnected player reconnects. Those features can be added separately without changing the core TCP transport design.
