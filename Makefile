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
	@echo "  make benchmark-strict - Run Benchmark (Strict Mode)"
	@echo "  make benchmark-batch  - Run Benchmark (Fast/Batch Mode)"
	@echo "  make benchmark-p99   - Run Latency Benchmark (p99)"
	@echo "  make benchmark-dos   - Run Security Benchmark (DoS)"
	@echo "  make clean       - Remove build artifacts"
	@echo "  make logs        - View the server logs"

build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make -j$(shell nproc)

run: build
	@echo "\n--- Starting Kallisto (Type 'HELP' for commands) ---\n"
	@./$(BUILD_DIR)/$(TARGET)

test: build
	@echo "\n--- Running Unit Tests ---\n"
	@./$(BUILD_DIR)/kallisto_test

benchmark-strict: build
	@echo "\n--- Running Benchmark (5000 Ops - STRICT MODE) ---\n"
	@echo "MODE STRICT\nBENCH 5000\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

benchmark-batch: build
	@echo "\n--- Running Benchmark (1,000,000 Ops - BATCH MODE) ---\n"
	@echo "MODE BATCH\nBENCH 1000000\nSAVE\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

benchmark-p99: build
	@echo "\n--- Running Benchmark (Latency P99) ---\n"
	@./$(BUILD_DIR)/bench_p99


benchmark-dos: build
	@echo "\n--- Running Benchmark (Security DoS) ---\n"
	@./$(BUILD_DIR)/bench_dos

test-atomic: build
	@echo "\n--- Running Atomic Stats & Thread Safety Test ---\n"
	@./$(BUILD_DIR)/repro_crash

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