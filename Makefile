# Kallisto Development Makefile
# Simplified wrapper for CMake workflow

BUILD_DIR = build
TARGET = kallisto
VCPKG_ROOT ?= /usr/local/vcpkg
CMAKE_TOOLCHAIN = -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake

.PHONY: all build build-server run clean help logs

all: build

help:
	@echo "Kallisto Commands:"
	@echo "  make build   - Configure and compile the project (core only)"
	@echo "  make build-server - Build with gRPC/HTTP server (requires vcpkg install)"
	@echo "  make run     - Run the interactive CLI"
	@echo "  make run-server  - Run the Kallisto server"
	@echo "  make test    - Run Unit Tests"
	@echo "  make test-listener - Run SO_REUSEPORT Listener Tests"
	@echo "  make test-threading - Run Threading Unit Tests"
	@echo "  make benchmark-strict - Run Benchmark (Strict Mode)"
	@echo "  make benchmark-batch  - Run Benchmark (Fast/Batch Mode)"
	@echo "  make benchmark-throughput - Run Throughput Benchmark"
	@echo "  make benchmark-p99   - Run Latency Benchmark (p99)"
	@echo "  make benchmark-dos   - Run Security Benchmark (DoS)"
	@echo "  make benchmark-multithread - Run Multi-threaded Benchmark"
	@echo "  make clean       - Remove build artifacts"
	@echo "  make logs        - View the server logs"

# Core build (without gRPC - always works)
build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make -j$(shell nproc)

# Server build (with gRPC via vcpkg)
build-server:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_TOOLCHAIN) .. && make -j$(shell nproc)

run: build
	@echo "\n--- Starting Kallisto (Type 'HELP' for commands) ---\n"
	@./$(BUILD_DIR)/$(TARGET)

run-server: build-server
	@echo "\n--- Starting Kallisto Server ---\n"
	@./$(BUILD_DIR)/kallisto_server --workers=$(shell nproc)

test: build
	@echo "\n--- Running Unit Tests ---\n"
	@./$(BUILD_DIR)/kallisto_test

test-listener: build-server
	@echo "\n--- Running SO_REUSEPORT Listener Tests ---\n"
	@./$(BUILD_DIR)/test_listener

test-threading: build
	@echo "\n--- Running Threading Unit Tests ---\n"
	@./$(BUILD_DIR)/test_threading

benchmark-strict: build
	@echo "\n--- Running Benchmark (5000 Ops - STRICT MODE) ---\n"
	@echo "MODE STRICT\nBENCH 5000\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

benchmark-batch: build
	@echo "\n--- Running Benchmark (1,000,000 Ops - BATCH MODE) ---\n"
	@echo "MODE BATCH\nBENCH 1000000\nSAVE\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

benchmark-p99: build
	@echo "\n--- Running Benchmark (Latency P99) ---\n"
	@./$(BUILD_DIR)/bench_p99

benchmark-throughput: build
	@echo "\n--- Running Throughput Benchmark (New 8-slot Cuckoo + Mutex) ---\n"
	@./$(BUILD_DIR)/bench_throughput

benchmark-dos: build
	@echo "\n--- Running Benchmark (Security DoS) ---\n"
	@./$(BUILD_DIR)/bench_dos

test-atomic: build
	@echo "\n--- Running Atomic Stats & Thread Safety Test ---\n"
	@./$(BUILD_DIR)/repro_crash

benchmark-multithread: build
	@echo "\n--- Running Multi-threaded Benchmark (3 workers) ---\n"
	@./$(BUILD_DIR)/bench_multithread

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
