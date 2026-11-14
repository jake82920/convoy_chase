CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CPPFLAGS = $(shell pkg-config --cflags libsystemd)
LDLIBS = $(shell pkg-config --libs libsystemd) -lreadline

all: server player

server: server.o comm.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

player: player.o comm.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

server.o: server.c comm.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c server.c

player.o: player.c comm.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c player.c

comm.o: comm.c comm.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c comm.c

clean:
	rm -f server player *.o

.PHONY: all clean
