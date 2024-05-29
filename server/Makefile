CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Werror
BUILD = .

all: $(BUILD)/aesdsocket

$(BUILD)/aesdsocket: $(BUILD)/aesdsocket.o
	$(CC) $^ -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@


.PHONY: clean

clean:
	rm -f $(BUILD)/aesdsocket.o
	rm -f $(BUILD)/aesdsocket
