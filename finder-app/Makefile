CC = $(CROSS_COMPILE)gcc
CFLAGS = -W -Wall -Wextra -Werror -Wpedantic -std=c99 -O2
CFLAGS += -D_FORTIFY_SOURCE=2

.PHONY: all

all: writer

writer: writer.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	-rm -f writer
