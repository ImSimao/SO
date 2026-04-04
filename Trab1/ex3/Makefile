CC=gcc
CFLAGS=-Wall 

TARGETS=vector-seq vector-seq-processes

.PHONY: all clean

all: $(TARGETS)

vector-seq: vector-seq.c
	$(CC) $(CFLAGS) -o $@ $<

vector-seq-processes: vector-seq-processes.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
