CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2

SERVER = server
CLIENT = player

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
SERVER_INSTALL_NAME = convoy-server
CLIENT_INSTALL_NAME = convoy-player

LEGACY_DBUS_POLICY = /etc/dbus-1/system.d/edu.asu.Convoy.conf

.PHONY: all server-build client-build check check-root \
        install install-server install-client \
        uninstall uninstall-server uninstall-client \
        clean clean-legacy help

all: server-build client-build

server-build: $(SERVER)
client-build: $(CLIENT)

$(SERVER): server.c comm.c comm.h
	$(CC) $(CFLAGS) -DCOMM_SERVER -o $@ server.c comm.c

$(CLIENT): player.c comm.c comm.h
	$(CC) $(CFLAGS) -o $@ player.c comm.c -lreadline

check: clean all
	@echo "Build check passed."

check-root:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "This target installs system packages/files and must be run with sudo."; \
		echo "Example: sudo make install-server"; \
		exit 1; \
	fi

# Remove the obsolete D-Bus policy from the Project 1 architecture if present.
# Linux users and unrelated system packages are intentionally left alone.
clean-legacy: check-root
	rm -f $(LEGACY_DBUS_POLICY)

# Install only the authoritative server role on this machine.
install-server: check-root
	@echo "Installing Convoy Chase server dependencies..."
	apt-get update
	apt-get install -y build-essential
	$(MAKE) clean-legacy
	$(MAKE) clean
	$(MAKE) server-build
	install -d $(BINDIR)
	rm -f $(BINDIR)/$(CLIENT_INSTALL_NAME)
	install -m 755 $(SERVER) $(BINDIR)/$(SERVER_INSTALL_NAME)
	@echo
	@echo "Convoy Chase server installed successfully."
	@echo "Run: $(SERVER_INSTALL_NAME)"
	@echo "TCP port: 8292"

# Install only the player/client role on this machine.
install-client: check-root
	@echo "Installing Convoy Chase client dependencies..."
	apt-get update
	apt-get install -y build-essential libreadline-dev
	$(MAKE) clean-legacy
	$(MAKE) clean
	$(MAKE) client-build
	install -d $(BINDIR)
	rm -f $(BINDIR)/$(SERVER_INSTALL_NAME)
	install -m 755 $(CLIENT) $(BINDIR)/$(CLIENT_INSTALL_NAME)
	@echo
	@echo "Convoy Chase client installed successfully."
	@echo "Run: $(CLIENT_INSTALL_NAME) <captain 1-4> <server-address>"

# Development convenience: install both roles on one machine.
install: check-root
	@echo "Installing Convoy Chase development dependencies..."
	apt-get update
	apt-get install -y build-essential libreadline-dev
	$(MAKE) clean-legacy
	$(MAKE) clean
	$(MAKE) all
	install -d $(BINDIR)
	install -m 755 $(SERVER) $(BINDIR)/$(SERVER_INSTALL_NAME)
	install -m 755 $(CLIENT) $(BINDIR)/$(CLIENT_INSTALL_NAME)
	@echo
	@echo "Convoy Chase server and client installed successfully."
	@echo "Server: $(SERVER_INSTALL_NAME)"
	@echo "Client: $(CLIENT_INSTALL_NAME) <captain 1-4> <server-address>"

uninstall: check-root uninstall-server uninstall-client

uninstall-server: check-root
	rm -f $(BINDIR)/$(SERVER_INSTALL_NAME)

uninstall-client: check-root
	rm -f $(BINDIR)/$(CLIENT_INSTALL_NAME)

clean:
	rm -f $(SERVER) $(CLIENT)

help:
	@echo "Convoy Chase targets:"
	@echo "  sudo make install-server  Fresh install of server role"
	@echo "  sudo make install-client  Fresh install of client role"
	@echo "  sudo make install         Install both roles for localhost testing"
	@echo "  make check                Compile both roles without installing"
	@echo "  sudo make uninstall       Remove installed Convoy Chase binaries"
	@echo "  make clean                Remove local build outputs"
