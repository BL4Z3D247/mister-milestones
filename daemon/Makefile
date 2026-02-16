CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wshadow -Werror -std=c11

KERNEL_INC := -I../kernel
RCHEEVOS_INC := -I../third_party/rcheevos/include

# math lib needed for rcheevos (fmodf)
LDLIBS ?= -lm

SRC := main.c ach_load.c memtap.c adapters.c engine.c util.c notify.c

# Find all rcheevos C files, but exclude:
# - rc_libretro* (requires libretro.h)
# - rc_client*   (network/client layer, not used in offline runtime test)
RC_SRC := $(shell find ../third_party/rcheevos/src -name '*.c' \
  | grep -v 'rc_libretro' \
  | grep -v '/rc_client' \
  | grep -v 'rc_client_' )

mmr-daemon: $(SRC) $(RC_SRC)
	$(CC) $(CFLAGS) $(KERNEL_INC) $(RCHEEVOS_INC) -o $@ $(SRC) $(RC_SRC) $(LDLIBS)

clean:
	rm -f mmr-daemon
