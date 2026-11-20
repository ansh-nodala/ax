CMAKE ?= cmake
BUILD ?= build
.PHONY: all build test clean examples verify
all: build
build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j
test: build
	./$(BUILD)/ax_tests
	bash scripts/run_tests.sh
clean:
	rm -rf $(BUILD)
examples: build
	bash scripts/run_tests.sh
verify:
	bash scripts/verify.sh
