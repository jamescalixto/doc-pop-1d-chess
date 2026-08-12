CXX = clang++
CXXFLAGS = -std=c++23 -Wall -Wextra -O3 -I src/cpp
SRC_DIR = src/cpp
BUILD_DIR = src/cpp/compiled

BINARIES = $(BUILD_DIR)/main $(BUILD_DIR)/explore $(BUILD_DIR)/retro
TESTS = $(BUILD_DIR)/perft $(BUILD_DIR)/verify_attacks

# Default target
all: $(BINARIES)

# Alpha-beta search driver.
$(BUILD_DIR)/main: $(SRC_DIR)/main.cpp $(SRC_DIR)/*.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Breadth-first enumeration of the game tree.
$(BUILD_DIR)/explore: $(SRC_DIR)/explore.cpp $(SRC_DIR)/*.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Retrograde analysis: slice sizing, verification and solving.
$(BUILD_DIR)/retro: $(SRC_DIR)/retro/retro.cpp $(SRC_DIR)/*.h $(SRC_DIR)/retro/*.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Movegen validated against the original mapping.txt implementation. Needs mapping.txt,
# which is no longer part of the engine; pass a path if it is not in the working
# directory.
$(BUILD_DIR)/perft: $(SRC_DIR)/tests/perft.cpp $(SRC_DIR)/*.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Computed attacks checked against every entry of the old lookup table.
$(BUILD_DIR)/verify_attacks: $(SRC_DIR)/tests/verify_attacks.cpp $(SRC_DIR)/*.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

tests: $(TESTS)

# Run the checks that do not need the retired lookup table.
check: $(BUILD_DIR)/retro
	./$(BUILD_DIR)/retro verify

run: $(BUILD_DIR)/main
	./$(BUILD_DIR)/main

count: $(BUILD_DIR)/retro
	./$(BUILD_DIR)/retro count

solve: $(BUILD_DIR)/retro
	./$(BUILD_DIR)/retro solve

mirrorcheck: $(BUILD_DIR)/retro
	./$(BUILD_DIR)/retro mirrorcheck

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all tests check run count solve mirrorcheck clean
