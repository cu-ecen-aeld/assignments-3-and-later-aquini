CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -W -Wall -Wextra -Werror -Wpedantic -std=c99 -O2
CFLAGS += -D_FORTIFY_SOURCE=2 -D_POSIX_C_SOURCE -D_DEFAULT_SOURCE
LDFLAGS ?= -lpthread -lrt

.PHONY: default

default: all

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	-rm -f aesdsocket
