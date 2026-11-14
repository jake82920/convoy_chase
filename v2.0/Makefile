CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -O2
LDLIBS = -lreadline

SERVER = server
CLIENT = player

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

SERVER_INSTALL_NAME = convoy-server
CLIENT_INSTALL_NAME = convoy-player

.PHONY: all client dependencies dependencies-server dependencies-client \
        install install-server install-client uninstall uninstall-server \
        uninstall-client clean

all: $(SERVER) $(CLIENT)

client: $(CLIENT)

$(SERVER): server.c comm.c comm.h
	$(CC) $(CFLAGS) -o $@ server.c comm.c $(LDLIBS)

$(CLIENT): player.c comm.c comm.h
	$(CC) $(CFLAGS) -o $@ player.c comm.c $(LDLIBS)

# comm.c contains the shared Readline-based event-loop support, so both
# builds currently require libreadline-dev.
dependencies: dependencies-server dependencies-client

dependencies-server:
	apt-get update
	apt-get install -y build-essential libreadline-dev

dependencies-client:
	apt-get update
	apt-get install -y build-essential libreadline-dev

# Install both roles on one machine for localhost development/testing.
install: install-server install-client

# Intended for the authoritative server VM.
install-server: dependencies-server $(SERVER)
	install -d $(BINDIR)
	install -m 755 $(SERVER) $(BINDIR)/$(SERVER_INSTALL_NAME)
	@echo "Installed server: $(BINDIR)/$(SERVER_INSTALL_NAME)"

# Intended for each player/client VM.
install-client: dependencies-client $(CLIENT)
	install -d $(BINDIR)
	install -m 755 $(CLIENT) $(BINDIR)/$(CLIENT_INSTALL_NAME)
	@echo "Installed client: $(BINDIR)/$(CLIENT_INSTALL_NAME)"

uninstall: uninstall-server uninstall-client

uninstall-server:
	rm -f $(BINDIR)/$(SERVER_INSTALL_NAME)

uninstall-client:
	rm -f $(BINDIR)/$(CLIENT_INSTALL_NAME)

clean:
	rm -f $(SERVER) $(CLIENT)

