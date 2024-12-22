CC=gcc
CFLAGS= -O2 -Wall -Werror

ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)gcc
endif

all: clean aesdsocket
default: aesdsocket

aesdsocket:
	$(CC) $(CCFLAGS) aesdsocket.c -o aesdsocket

.PHONY: clean
clean:
	rm -rf aesdsocket *.o