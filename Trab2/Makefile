CC = cc
CFLAGS = -Wall

all: server client ex2_compare ex3_server ex3_client

server: Ex1/server.c
	$(CC) $(CFLAGS) -o $@ $<

client: Ex1/client.c
	$(CC) $(CFLAGS) -o $@ $<

ex2_compare: Ex2/ex2_compare.c
	$(CC) $(CFLAGS) -pthread -g -o $@ $<

ex3_server: Ex3/ex3_server.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

ex3_client: Ex3/ex3_client.c
	$(CC) $(CFLAGS) -pthread -o $@ $<

clean:
	rm -f server client ex2_compare ex3_server ex3_client

.PHONY: all clean
