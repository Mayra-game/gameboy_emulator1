CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
SOURCES = src/main.c src/cpu.c src/memory.c
TARGET = gameboy_emulator

all: $(TARGET)

$(TARGET): $(SOURCES) src/cpu.h src/memory.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@

run: $(TARGET)
	./$(TARGET) $(ROM)

test: $(TARGET)
	./$(TARGET) --self-test

clean:
	rm -f $(TARGET)

.PHONY: all run test clean
