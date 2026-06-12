CC = cc
CFLAGS = -Wall -Wextra -pthread

all: test_threadpool

test_threadpool: test_threadpool.c threadpool.c threadpool.h
	$(CC) $(CFLAGS) -o $@ test_threadpool.c threadpool.c

clean:
	rm -f test_threadpool

.PHONY: all clean
