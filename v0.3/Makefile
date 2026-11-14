CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
LDLIBS = -lsystemd -lreadline

SERVER = server
PLAYER = player

USERS = server player1 player2 player3 player4

DBUS_POLICY = edu.asu.Convoy.conf
DBUS_POLICY_DIR = /etc/dbus-1/system.d

.PHONY: all clean install uninstall users install-binaries install-dbus

all: $(SERVER) $(PLAYER)

$(SERVER): server.c comm.c comm.h
	$(CC) $(CFLAGS) -o $(SERVER) server.c comm.c $(LDLIBS)

$(PLAYER): player.c comm.c comm.h
	$(CC) $(CFLAGS) -o $(PLAYER) player.c comm.c $(LDLIBS)


# ---------------------------------------------------------
# Installation
# Run with: sudo make install
# ---------------------------------------------------------

install: all check-root users install-binaries install-dbus
	@echo ""
	@echo "Game environment installed."
	@echo ""
	@echo "Set passwords with:"
	@echo "  passwd server"
	@echo "  passwd player1"
	@echo "  passwd player2"
	@echo "  passwd player3"
	@echo "  passwd player4"


# ---------------------------------------------------------
# Root check
# ---------------------------------------------------------

check-root:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Installation must be run as root."; \
		echo "Use: sudo make install"; \
		exit 1; \
	fi


# ---------------------------------------------------------
# Create normal Linux users
# ---------------------------------------------------------

users:
	@for user in $(USERS); do \
		if ! id "$$user" >/dev/null 2>&1; then \
			echo "Creating user $$user..."; \
			useradd -m -s /bin/bash "$$user"; \
		else \
			echo "User $$user already exists."; \
		fi; \
	done


# ---------------------------------------------------------
# Install executables
# Each user gets only the executable they need.
# ---------------------------------------------------------

install-binaries:
	@echo "Installing server..."
	install -o server -g server -m 700 $(SERVER) /home/server/server

	@echo "Installing player 1..."
	install -o player1 -g player1 -m 700 $(PLAYER) /home/player1/player

	@echo "Installing player 2..."
	install -o player2 -g player2 -m 700 $(PLAYER) /home/player2/player

	@echo "Installing player 3..."
	install -o player3 -g player3 -m 700 $(PLAYER) /home/player3/player

	@echo "Installing player 4..."
	install -o player4 -g player4 -m 700 $(PLAYER) /home/player4/player


# ---------------------------------------------------------
# Install system D-Bus policy
# ---------------------------------------------------------

install-dbus:
	@echo "Installing D-Bus policy..."
	install -o root -g root -m 644 $(DBUS_POLICY) \
		$(DBUS_POLICY_DIR)/$(DBUS_POLICY)

	@echo "Reloading D-Bus configuration..."
	@if command -v systemctl >/dev/null 2>&1; then \
		systemctl reload dbus 2>/dev/null || true; \
	fi


# ---------------------------------------------------------
# Remove game environment
# WARNING: removes the five game users and their homes.
# ---------------------------------------------------------

uninstall: check-root
	@echo "Removing D-Bus policy..."
	rm -f $(DBUS_POLICY_DIR)/$(DBUS_POLICY)

	@if command -v systemctl >/dev/null 2>&1; then \
		systemctl reload dbus 2>/dev/null || true; \
	fi

	@for user in $(USERS); do \
		if id "$$user" >/dev/null 2>&1; then \
			echo "Removing user $$user..."; \
			userdel -r "$$user" 2>/dev/null || true; \
		fi; \
	done

	@echo "Game environment removed."


# ---------------------------------------------------------
# Build cleanup
# ---------------------------------------------------------

clean:
	rm -f $(SERVER) $(PLAYER)
