# Kallisto Development Makefile
# Simplified wrapper for CMake workflow

BUILD_DIR = build
TARGET = kallisto

.PHONY: all build run clean help logs

all: build

help:
	@echo "Kallisto MVP Prototype Commands:"
	@echo "  make build   - Configure and compile the project"
	@echo "  make run     - Build and execute the prototype demo"
	@echo "  make clean   - Remove build artifacts"
	@echo "  make logs    - View the server logs"

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make -j$(shell nproc)

run: build
	@echo "\n--- Starting Kallisto Prototype Demo ---\n"
	@./$(BUILD_DIR)/$(TARGET)

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