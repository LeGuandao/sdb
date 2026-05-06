CC      := gcc
CFLAGS  := -Wall -Wextra -g -Iinclude
LDFLAGS := -lreadline

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)

.PHONY: all clean demo

all: sdb demo

sdb: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

demo: demo.c
	$(CC) $(CFLAGS) -no-pie -o $@ $<

clean:
	rm -f sdb demo src/*.o
