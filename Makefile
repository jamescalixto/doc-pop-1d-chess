CXX = clang++
CXXFLAGS = -std=c++23 -Wall -Wextra -O3 -I src/cpp
SRC_DIR = src/cpp
BUILD_DIR = src/cpp/compiled
TARGET = $(BUILD_DIR)/main

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SRC_DIR)/main.cpp
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Run the executable
run: $(TARGET)
	./$(TARGET)

# Clean up build artifacts
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
