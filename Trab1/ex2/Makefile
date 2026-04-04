CC=gcc
CFLAGS=-Wall 
TARGET=count_words
SRC=count_words.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)
