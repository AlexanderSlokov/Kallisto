# Kallisto Development Makefile
# Simplified wrapper for CMake workflow

BUILD_DIR = build
TARGET = kallisto

.PHONY: all build run clean help logs

all: build

help:
	@echo "Kallisto Commands:"
	@echo "  make build   - Configure and compile the project"
	@echo "  make run     - Run the interactive CLI"
	@echo "  make test    - Run Unit Tests"
	@echo "  make bench   - Run Benchmark (10,000 Ops)"
	@echo "  make clean   - Remove build artifacts"
	@echo "  make logs    - View the server logs"

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make -j$(shell nproc)

run: build
	@echo "\n--- Starting Kallisto (Type 'HELP' for commands) ---\n"
	@./$(BUILD_DIR)/$(TARGET)

test: build
	@echo "\n--- Running Unit Tests ---\n"
	@./$(BUILD_DIR)/kallisto_test

benchmark: build
	@echo "\n--- Running Benchmark (10,000 Ops) ---\n"
	@echo "BENCH 10000\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf logs/
	@rm -f kallisto.server.log

logs:
	@if [ -f kallisto.server.log ]; then \
		cat kallisto.server.log; \
	elif [ -f logs/kallisto.server.log ]; then \
		cat logs/kallisto.server.log; \
	else \
		echo "No logs found yet. Run 'make run' first."; \
	fi