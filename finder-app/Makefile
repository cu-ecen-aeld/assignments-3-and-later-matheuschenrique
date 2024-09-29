CC=gcc
CFLAGS= -O2 -Wall

ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)gcc
endif

all: writer

writer:
	$(CC) $(CFLAGS) writer.c -o writer

.PHONY: clean
clean:
	rm -rf writer *.o