CC = cc
CFLAGS = -Wall -Wextra -pthread -I../Ex2

all: ex3_compare

ex3_compare: ex3_compare.c ../Ex2/threadpool.c ../Ex2/threadpool.h
	$(CC) $(CFLAGS) -o $@ ex3_compare.c ../Ex2/threadpool.c

clean:
	rm -f ex3_compare

.PHONY: all clean
