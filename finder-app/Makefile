CC=gcc
CFLAGS= -02 -Wall

ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)gcc
endif

writer:
	$(CC) -Wall writer.c -o writer

.PHONY: clean
clean:
	rm -rf writer *.o