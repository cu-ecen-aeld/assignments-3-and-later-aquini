CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -W -Wall -Wextra -Werror -Wpedantic -std=c99 -O2
CFLAGS += -D_FORTIFY_SOURCE=2 -D_POSIX_C_SOURCE -D_DEFAULT_SOURCE
LDFLAGS ?= -lpthread -lrt

ifeq ($(USE_AESD_CHAR_DEVICE), 1)
 USRDEFS = -DUSE_AESD_CHAR_DEVICE
endif

.PHONY: default

default: all

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $(USRDEFS) $< -o $@ $(LDFLAGS)

clean:
	-rm -f aesdsocket
