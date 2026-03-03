.PHONY: all build clean test debug release ping_pong

BUILD_DIR ?= build
BUILD_TYPE ?= Release

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) -j$$(nproc)

debug:
	$(MAKE) build BUILD_TYPE=Debug

release:
	$(MAKE) build BUILD_TYPE=Release

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

ping_pong: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --target ping_pong -j$$(nproc)

clean:
	rm -rf $(BUILD_DIR)
	rm -f sub.out test_eventfd
