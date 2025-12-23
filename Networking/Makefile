# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lcrypto

# Executables
TARGETS = server client

all: $(TARGETS)

server: server.c sham.h
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

client: client.c sham.h
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.txt *.log

.PHONY: all clean
