CC = $(CROSS_COMPILE)gcc

BUILD = .

all: $(BUILD)/writer

$(BUILD)/writer: $(BUILD)/writer.o
	$(CC) $^ -o $@

%.o: %.c
	$(CC) -c $< -o $@


.PHONY: clean

clean:
	rm -f $(BUILD)/writer.o
	rm -f $(BUILD)/writer
