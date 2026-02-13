CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wshadow -Werror -std=c11
LDFLAGS ?=
INCLUDES := -I../kernel

BIN := mmr-daemon
SRC := main.c memtap.c adapters.c util.c notify.c

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: all clean
