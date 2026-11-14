# Convoy Chase - Distributed Version

Convoy Chase is a four-player terminal game with one authoritative server and four remote player clients. This version uses a small custom protocol over raw TCP sockets instead of the local D-Bus transport used by the earlier version.

The same repository is used on every machine. Install the **server role** on one VM and the **client role** on each player VM.

## Files

- `server.c` - game rules and authoritative game state
- `player.c` - player terminal interface
- `comm.c` / `comm.h` - TCP communication layer, message framing, `poll()` loops, and timers
- `Makefile` - builds and installs the server or client role, including required packages

The server listens on TCP port **8292**.

## Server VM

On a clean Ubuntu Server VM, clone the repository and enter this version's directory. Then run:

```bash
sudo make install-server
```

That command installs the required compiler tools, removes obsolete Convoy Chase/D-Bus installation artifacts if they exist, builds the server, and installs:

```text
/usr/local/bin/convoy-server
```

Start the game server with:

```bash
convoy-server
```

Find the server VM's address with:

```bash
hostname -I
```

Use that address when starting the four clients.

If Ubuntu's UFW firewall is enabled, allow the game port once:

```bash
sudo ufw allow 8292/tcp
```

## Client VM

On each player VM, clone the same repository and run:

```bash
sudo make install-client
```

That command installs the compiler and Readline development package, removes obsolete Convoy Chase/D-Bus installation artifacts if they exist, builds the client, and installs:

```text
/usr/local/bin/convoy-player
```

Connect with:

```bash
convoy-player <captain-number> <server-address>
```

For a server at `192.168.122.50`, the four VMs would run:

```bash
convoy-player 1 192.168.122.50
convoy-player 2 192.168.122.50
convoy-player 3 192.168.122.50
convoy-player 4 192.168.122.50
```

Captain numbers are 1 through 4, and only one active connection may claim a captain number.

## Quick Deployment

The complete five-VM setup is therefore:

```text
Server VM:   sudo make install-server   -> convoy-server
Player 1 VM: sudo make install-client   -> convoy-player 1 <server-ip>
Player 2 VM: sudo make install-client   -> convoy-player 2 <server-ip>
Player 3 VM: sudo make install-client   -> convoy-player 3 <server-ip>
Player 4 VM: sudo make install-client   -> convoy-player 4 <server-ip>
```

No D-Bus configuration, special Linux users, RPC framework, or separate server/client repository is required.

## Game Instructions

### A SHIP ON THE HIGH SEAS

A Royal Navy convoy carrying valuable cargo is racing toward port. But one Captain is secretly a **PIRATE INFILTRATOR**, working to delay the fleet until pursuing pirates can catch it.

**ROYAL NAVY:** Advance the convoy 4 times and reach port.

**PIRATE:** Cause 4 delays before the convoy reaches port.

### HOW A ROUND WORKS

One Captain becomes Commodore and proposes an action.

**SAIL**

The Commodore selects 2 or 3 Captains for a mission. Selected Captains secretly commit manpower. The mission needs 6 total manpower to succeed. Success advances the convoy; failure advances the pirates.

**ANCHOR**

The fleet stops so exhausted crews can rest. Every Captain recovers 3 manpower, up to a maximum of 6. But the delay also advances the pirates.

### VOTING AND STRATEGY

Every proposal must be approved by the fleet. A tie is a rejection. Three rejected or missed proposals also advance the pirates.

Manpower is limited. Watch who contributes, who pushes to anchor, and which missions mysteriously fail.

Discuss, accuse, bluff, and cooperate. One of you is working against the fleet.

**Trust carefully.**

## Local Testing

To install both roles on one machine:

```bash
sudo make install
```

Start the server:

```bash
convoy-server
```

Then use four other terminals:

```bash
convoy-player 1 127.0.0.1
convoy-player 2 127.0.0.1
convoy-player 3 127.0.0.1
convoy-player 4 127.0.0.1
```

This is useful for checking game behavior before testing VM networking.

## Build Check

To compile both programs without installing them:

```bash
make check
```

The server build does not require Readline. The client uses Readline for interactive terminal input.

## Networking Check

Before starting a full game, verify that a client can reach the server VM:

```bash
ping <server-ip>
```

If the server is running, you can also test its TCP port with `nc` if netcat is installed:

```bash
nc -vz <server-ip> 8292
```

`nc` is only a troubleshooting tool and is not required by Convoy Chase.

## Uninstall

Server VM:

```bash
sudo make uninstall-server
```

Client VM:

```bash
sudo make uninstall-client
```

Development machine with both roles:

```bash
sudo make uninstall
```

Uninstall removes Convoy Chase binaries. It intentionally does not remove GCC, Readline, Linux users, or other general system packages.

## Architecture

```text
Player 1 VM ---------\
Player 2 VM ----------\
Player 3 VM -----------+---- TCP/IP ----> Convoy Server : 8292
Player 4 VM ----------/
```

`server.c` remains responsible for the game. The communication details are isolated in `comm.c`, allowing the transport to change without embedding socket code throughout the game logic.

TCP is a byte stream, so `comm.c` adds explicit message framing. Each message contains a 32-bit message type and a 32-bit payload length in network byte order, followed by the payload. The receiver retains partial data until a complete message has arrived.

The implementation directly uses POSIX/Linux facilities including `socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`, `poll()`, and `timerfd` rather than a networking framework.

## Current Scope

The server rejects invalid captain numbers and duplicate simultaneous captain connections. If a player disconnects during an active game, the server detects the lost TCP connection, but automatic game-state restoration/reconnection is not currently implemented.
