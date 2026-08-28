# Makefile for NVMe-oF test program

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
LDFLAGS = -lrt

# Target executable
TARGET = nvmeof_test
SOURCE = nvmeof_test.c

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

# Clean build artifacts
clean:
	rm -f $(TARGET)

# Install the executable (optional)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Run tests with sample parameters
test: $(TARGET)
	./$(TARGET) . 10 4096 10

# Display help
help:
	@echo "Makefile targets:"
	@echo "  all     - Build the executable (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  install - Install the executable to /usr/local/bin"
	@echo "  test    - Run a quick test with sample parameters"
	@echo "  help    - Display this help message"

.PHONY: all clean install test help
