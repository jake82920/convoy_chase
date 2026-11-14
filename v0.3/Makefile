CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
LDLIBS = -lsystemd -lreadline

SERVER = server
PLAYER = player

USERS = server player1 player2 player3 player4

DBUS_POLICY = edu.asu.Convoy.conf
DBUS_POLICY_DIR = /etc/dbus-1/system.d

.PHONY: all install dependencies check-root users \
        install-binaries install-dbus clean uninstall


# =========================================================
# Normal build
# =========================================================

all: $(SERVER) $(PLAYER)

$(SERVER): server.c comm.c comm.h
	$(CC) $(CFLAGS) -o $(SERVER) server.c comm.c $(LDLIBS)

$(PLAYER): player.c comm.c comm.h
	$(CC) $(CFLAGS) -o $(PLAYER) player.c comm.c $(LDLIBS)


# =========================================================
# Full Ubuntu installation
#
# Run:
#     sudo make install
# =========================================================

install: check-root dependencies all users install-binaries install-dbus
	@echo ""
	@echo "========================================="
	@echo " Naval Convoy installation complete"
	@echo "========================================="
	@echo ""
	@echo "Set account passwords with:"
	@echo "  passwd server"
	@echo "  passwd player1"
	@echo "  passwd player2"
	@echo "  passwd player3"
	@echo "  passwd player4"


# =========================================================
# Verify installation is running as root
# =========================================================

check-root:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "ERROR: installation must be run as root."; \
		echo "Run: sudo make install"; \
		exit 1; \
	fi


# =========================================================
# Ubuntu dependencies
# =========================================================

dependencies:
	@echo "Installing required Ubuntu packages..."
	apt-get update
	DEBIAN_FRONTEND=noninteractive apt-get install -y \
		build-essential \
		libsystemd-dev \
		libreadline-dev


# =========================================================
# Create normal interactive Linux users
# =========================================================

users:
	@for user in $(USERS); do \
		if ! id "$$user" >/dev/null 2>&1; then \
			echo "Creating user $$user..."; \
			useradd -m -s /bin/bash "$$user"; \
		else \
			echo "User $$user already exists."; \
		fi; \
	done


# =========================================================
# Install executables
#
# Each account receives only its appropriate executable.
# =========================================================

install-binaries:
	@echo "Installing server executable..."
	install -o server -g server -m 700 \
		$(SERVER) /home/server/server

	@echo "Installing player1 executable..."
	install -o player1 -g player1 -m 700 \
		$(PLAYER) /home/player1/player

	@echo "Installing player2 executable..."
	install -o player2 -g player2 -m 700 \
		$(PLAYER) /home/player2/player

	@echo "Installing player3 executable..."
	install -o player3 -g player3 -m 700 \
		$(PLAYER) /home/player3/player

	@echo "Installing player4 executable..."
	install -o player4 -g player4 -m 700 \
		$(PLAYER) /home/player4/player


# =========================================================
# Install system D-Bus policy
# =========================================================

install-dbus:
	@echo "Installing D-Bus policy..."

	install -o root -g root -m 644 \
		$(DBUS_POLICY) \
		$(DBUS_POLICY_DIR)/$(DBUS_POLICY)

	@echo "Reloading D-Bus configuration..."

	@if command -v systemctl >/dev/null 2>&1; then \
		systemctl reload dbus 2>/dev/null || true; \
	fi


# =========================================================
# Uninstall
#
# WARNING:
# This deletes the five game users and their home directories.
#
# Run:
#     sudo make uninstall
# =========================================================

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


# =========================================================
# Remove locally compiled binaries
# =========================================================

clean:
	rm -f $(SERVER) $(PLAYER)
