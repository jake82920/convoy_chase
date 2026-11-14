CC = gcc

CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2

SERVER_CFLAGS = $(CFLAGS) -DCOMM_SERVER_ONLY
CLIENT_CFLAGS = $(CFLAGS)

SERVER = convoy-server
CLIENT = convoy-player

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all server client clean \
        install install-server install-client \
        uninstall uninstall-server uninstall-client


# ============================================================
# Build
# ============================================================

all: $(SERVER) $(CLIENT)

server: $(SERVER)

client: $(CLIENT)


$(SERVER): server.c comm.c comm.h
	$(CC) $(SERVER_CFLAGS) -o $@ server.c comm.c


$(CLIENT): player.c comm.c comm.h
	$(CC) $(CLIENT_CFLAGS) -o $@ player.c comm.c -lreadline


# ============================================================
# Installation
#
# These targets are intended for fresh Ubuntu Server VMs.
# Dependencies are installed automatically.
#
# Server:
#   sudo make install-server
#
# Client:
#   sudo make install-client
#
# ============================================================

install-server:
	@echo "Installing Convoy Chase server dependencies..."
	apt-get update
	apt-get install -y build-essential
	@echo
	@echo "Building Convoy Chase server..."
	$(MAKE) clean
	$(MAKE) server
	@echo
	@echo "Installing $(SERVER) to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 0755 $(SERVER) $(BINDIR)/$(SERVER)
	@echo
	@echo "============================================"
	@echo " Convoy Chase server installed successfully"
	@echo "============================================"
	@echo
	@echo "Run:"
	@echo "  $(SERVER)"
	@echo


install-client:
	@echo "Installing Convoy Chase client dependencies..."
	apt-get update
	apt-get install -y build-essential libreadline-dev
	@echo
	@echo "Building Convoy Chase client..."
	$(MAKE) clean
	$(MAKE) client
	@echo
	@echo "Installing $(CLIENT) to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 0755 $(CLIENT) $(BINDIR)/$(CLIENT)
	@echo
	@echo "============================================"
	@echo " Convoy Chase client installed successfully"
	@echo "============================================"
	@echo
	@echo "Run:"
	@echo "  $(CLIENT) <captain 1-4> <server-address>"
	@echo
	@echo "Example:"
	@echo "  $(CLIENT) 1 192.168.122.100"
	@echo


# Install both roles on one machine.
# Useful for local development/testing.

install:
	apt-get update
	apt-get install -y build-essential libreadline-dev
	$(MAKE) clean
	$(MAKE) all
	install -d $(BINDIR)
	install -m 0755 $(SERVER) $(BINDIR)/$(SERVER)
	install -m 0755 $(CLIENT) $(BINDIR)/$(CLIENT)
	@echo
	@echo "Convoy Chase server and client installed."
	@echo


# ============================================================
# Uninstall
# ============================================================

uninstall-server:
	rm -f $(BINDIR)/$(SERVER)
	@echo "Convoy Chase server removed."


uninstall-client:
	rm -f $(BINDIR)/$(CLIENT)
	@echo "Convoy Chase client removed."


uninstall: uninstall-server uninstall-client


# ============================================================
# Cleanup
# ============================================================

clean:
	rm -f $(SERVER) $(CLIENT)
