CC = gcc

CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
LDLIBS = -lreadline

SERVER = server
PLAYER = player

.PHONY: all install-server install-client install clean


# ============================================================
# Build
#
# These targets assume dependencies are already installed.
# Use install-server or install-client on a fresh machine.
# ============================================================

all: $(SERVER) $(PLAYER)

$(SERVER): server.c comm.c comm.h
	$(CC) $(CFLAGS) -o $@ server.c comm.c $(LDLIBS)

$(PLAYER): player.c comm.c comm.h
	$(CC) $(CFLAGS) -o $@ player.c comm.c $(LDLIBS)


# ============================================================
# Fresh Ubuntu Server Setup
#
# Usage:
#   sudo make install-server
#
# Then:
#   ./server
# ============================================================

install-server:
	@echo "Installing all server dependencies..."
	apt-get update
	DEBIAN_FRONTEND=noninteractive apt-get install -y \
		build-essential \
		gcc \
		make \
		libreadline-dev
	@echo
	@echo "Building server..."
	$(MAKE) clean
	$(MAKE) $(SERVER)
	@echo
	@echo "========================================"
	@echo " Convoy Chase server is ready"
	@echo "========================================"
	@echo
	@echo "Run:"
	@echo "  ./server"
	@echo


# ============================================================
# Fresh Ubuntu Client Setup
#
# Usage:
#   sudo make install-client
#
# Then:
#   ./player <captain 1-4> <server-address>
# ============================================================

install-client:
	@echo "Installing all client dependencies..."
	apt-get update
	DEBIAN_FRONTEND=noninteractive apt-get install -y \
		build-essential \
		gcc \
		make \
		libreadline-dev
	@echo
	@echo "Building player..."
	$(MAKE) clean
	$(MAKE) $(PLAYER)
	@echo
	@echo "========================================"
	@echo " Convoy Chase player is ready"
	@echo "========================================"
	@echo
	@echo "Run:"
	@echo "  ./player <captain 1-4> <server-address>"
	@echo
	@echo "Example:"
	@echo "  ./player 1 192.168.122.100"
	@echo


# ============================================================
# Full Local Installation
#
# Installs every dependency and builds both server and player.
#
# Usage:
#   sudo make install
# ============================================================

install:
	@echo "Installing all Convoy Chase dependencies..."
	apt-get update
	DEBIAN_FRONTEND=noninteractive apt-get install -y \
		build-essential \
		gcc \
		make \
		libreadline-dev
	@echo
	@echo "Building server and player..."
	$(MAKE) clean
	$(MAKE) all
	@echo
	@echo "========================================"
	@echo " Convoy Chase is ready"
	@echo "========================================"
	@echo
	@echo "Server:"
	@echo "  ./server"
	@echo
	@echo "Player:"
	@echo "  ./player <captain 1-4> <server-address>"
	@echo


# ============================================================
# Cleanup
# ============================================================

clean:
	rm -f $(SERVER) $(PLAYER)
