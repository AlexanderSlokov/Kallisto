# Kallisto Development Makefile - "The Professional & Legacy Edition"
# Unified workflow for Terminal, IDE, and Docker

BUILD_DIR = build
TARGET = kallisto
VCPKG_ROOT ?= /usr/local/vcpkg
DB_PATH ?= /data/kallisto/rocksdb

# Modern CMake Toolchain Integration
CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake

.PHONY: all build build-server run run-server clean help logs test \
        test-main test-rocksdb test-listener test-threading test-persistence \
        benchmark-strict benchmark-batch benchmark-p99 benchmark-throughput \
        benchmark-dos test-atomic benchmark-multithread \
        bench-ghz bench-server bench-http bench-grpc \
        docker-build docker-test docker-run

all: build

help:
	@echo "Kallisto Commands:"
	@echo "  make build          - Build core (CLI only)"
	@echo "  make build-server   - Build with gRPC/HTTP + RocksDB (requires vcpkg)"
	@echo "  make test           - Run all Unit Tests (via CTest)"
	@echo "  make clean          - Deep clean (Fixes Generator mismatch)"
	@echo ""
	@echo "Legacy & Specialized Commands:"
	@echo "  make benchmark-batch - 1M ops stress test"
	@echo "  make benchmark-dos   - Security/DoS test"
	@echo "  make docker-build    - Build production Docker image"

# ===========================================================================
# Build System (Generator-agnostic)
# ===========================================================================
build:
	@cmake -B $(BUILD_DIR) -S .
	@cmake --build $(BUILD_DIR) -j $(shell nproc)

build-server:
	@cmake -B $(BUILD_DIR) -S . $(CMAKE_FLAGS)
	@cmake --build $(BUILD_DIR) -j $(shell nproc)

run: build
	@./$(BUILD_DIR)/$(TARGET)

run-server:
	@./$(BUILD_DIR)/kallisto_server --workers=$(shell nproc) --db-path=$(DB_PATH)

# ===========================================================================
# Unit Tests
# ===========================================================================
test: build-server
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

test-main: build
	@./$(BUILD_DIR)/kallisto_test

test-rocksdb: build-server
	@./$(BUILD_DIR)/test_rocksdb

test-listener: build-server
	@./$(BUILD_DIR)/test_listener

test-threading: build
	@./$(BUILD_DIR)/test_threading

test-persistence: build-server
	@bash tests/test_persistence.sh

# ===========================================================================
# Benchmarks (CLI & In-process)
# ===========================================================================
benchmark-strict: build
	@echo "MODE STRICT\nBENCH 5000\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

benchmark-batch: build
	@echo "MODE BATCH\nBENCH 1000000\nSAVE\nEXIT" | ./$(BUILD_DIR)/$(TARGET)

benchmark-p99: build
	@./$(BUILD_DIR)/bench_p99

benchmark-throughput: build
	@./$(BUILD_DIR)/bench_throughput

benchmark-dos: build
	@./$(BUILD_DIR)/bench_dos

test-atomic: build
	@./$(BUILD_DIR)/repro_crash

benchmark-multithread: build
	@./$(BUILD_DIR)/bench_multithread

# ===========================================================================
# Benchmarks (Server - HTTP/gRPC)
# ===========================================================================
bench-ghz:
	@chmod +x bench/run_ghz.sh && ./bench/run_ghz.sh

bench-server:
	@bash bench/run_server_bench.sh

bench-http: bench-server
bench-grpc: bench-ghz

# ===========================================================================
# Docker Integration
# ===========================================================================
docker-build:
	@docker build -t kallisto-server:latest .

docker-test:
	@docker build --target tester -t kallisto-tester:latest .
	@docker run --rm kallisto-tester:latest make test

docker-run:
	@docker run -d --name kallisto -p 8200:8200 -p 8201:8201 \
	  -v my-kallisto-data:/data/kallisto/rocksdb kallisto-server:latest

# ===========================================================================
# Utilities
# ===========================================================================
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Build directory cleared."

logs:
	@tail -f kallisto.server.log 2>/dev/null || echo "No logs found."
