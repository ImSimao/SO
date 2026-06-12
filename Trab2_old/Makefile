# TP2 I.1 — build na máquina de laboratório (ex.: 10.64.137.7 na rede ISEL) e abrir o porto TCP no firewall se usar modo tcp/both.
CC = cc
CFLAGS = -Wall 

all: server client

server: server.c
	$(CC) $(CFLAGS) -o $@ $<

client: client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f server client

.PHONY: all clean
