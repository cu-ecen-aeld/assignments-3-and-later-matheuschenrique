CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror
LDFLAGS ?= -lpthread -lrt
TARGET ?= aesdsocket
SRC ?= aesdsocket.c

all: clean $(TARGET)
default: $(TARGET)

aesdsocket:
	$(CC) $(CCFLAGS) $(SRC) -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf $(TARGET) *.o