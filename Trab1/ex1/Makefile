CC=gcc
CFLAGS=-Wall 
TARGET=parallel_exec
SRC=parallel_exec.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
