CC = cc
CFLAGS = -Wall -Wextra -pthread

all: test_countdown

test_countdown: test_countdown.c countdown.c countdown.h
	$(CC) $(CFLAGS) -o $@ test_countdown.c countdown.c

clean:
	rm -f test_countdown

.PHONY: all clean
