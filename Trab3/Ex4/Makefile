CC = cc
CFLAGS = -Wall -Wextra -pthread -I../Ex2

all: ex4_server ex4_client

ex4_server: ex4_server.c ../Ex2/threadpool.c ../Ex2/threadpool.h
	$(CC) $(CFLAGS) -o $@ ex4_server.c ../Ex2/threadpool.c

ex4_client: ex4_client.c
	$(CC) $(CFLAGS) -o $@ ex4_client.c

clean:
	rm -f ex4_server ex4_client

.PHONY: all clean
