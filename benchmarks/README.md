# Kallisto Benchmarks

This directory contains all benchmarking, stress-testing, and diagnostic tools for Kallisto.

## Directory Structure

```
benchmarks/
├── core/                    # In-process C++ micro-benchmarks
│   ├── bench_p99.cpp        # p99 latency measurement (ShardedCuckooTable)
│   ├── bench_throughput.cpp # Single-thread insert throughput
│   └── bench_multithread.cpp# Multi-threaded workload (Vault traffic patterns)
│
├── security/                # Security & resilience benchmarks
│   └── bench_dos.cpp        # Hash flooding & B-Tree gate rejection
│
├── server/                  # HTTP server load testing (wrk-based)
│   ├── run_server_bench.sh  # Orchestrator: start server, seed, bench, cleanup
│   └── workloads/           # wrk Lua workload scripts
│       ├── wrk_seed.lua     # Data seeder (POST s0..s999)
│       ├── wrk_get.lua      # Pure READ benchmark
│       ├── wrk_put.lua      # Pure WRITE benchmark
│       └── wrk_mixed.lua    # 95% READ / 5% WRITE mixed workload
│
└── diagnostic/              # Stress tests & crash reproduction
    └── repro_crash.cpp      # Thread-safety stress test (ShardedCuckooTable)
```

## Quick Start

```bash
# Run all HTTP benchmarks (GET / PUT / MIXED via wrk)
make bench-server

# Run individual in-process benchmarks
make benchmark-p99           # p99 latency
make benchmark-multithread   # Multi-threaded Vault workload patterns
make benchmark-dos           # DoS / hash flooding resilience

# Run diagnostics
make test-atomic             # Thread-safety stress (repro_crash)
```

## Benchmark Categories

### Core (`core/`)
In-process C++ benchmarks that measure raw data structure performance without network overhead.
Useful for regression testing and architecture comparison.

### Security (`security/`)
Validates resilience against adversarial workloads (hash flooding, invalid path injection).
Compares SipHash vs weak hash to demonstrate DoS protection.

### Server (`server/`)
End-to-end HTTP benchmarks using `wrk`. Measures real-world throughput including JSON parsing,
HTTP handling, RocksDB persistence, and SO_REUSEPORT distribution.

### Diagnostic (`diagnostic/`)
Tools for reproducing concurrency bugs and validating thread-safety under extreme contention.
Not performance benchmarks — these validate correctness.
