CFLAGS = -g -O0
LDFLAGS =
BUILD_DIR := .

$(BUILD_DIR)/main: $(BUILD_DIR)/main.o
	$(CC) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/main.o: main.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm $(BUILD_DIR)/main

.PHONY: clean
