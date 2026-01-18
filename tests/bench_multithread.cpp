/*
 * Source: tests/bench_multithread.cpp
 * Purpose: Multi-threaded benchmark using WorkerPool
 * Tests: Concurrent insert/lookup performance with N workers
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <thread>
#include <latch>

#include "kallisto/event/worker.hpp"
#include "kallisto/cuckoo_table.hpp"
#include "kallisto/logger.hpp"

// Forward declaration
namespace kallisto {
    event::WorkerPoolPtr createWorkerPool(size_t num_workers);
}

int main(int argc, char** argv) {
    // Config
    const size_t NUM_WORKERS = 3;  // The magic number!
    const size_t TOTAL_ITEMS = 999999;  // Divisible by 3
    const size_t ITEMS_PER_WORKER = TOTAL_ITEMS / NUM_WORKERS;
    
    // Setup logging
    kallisto::LogConfig config("bench_mt");
    config.logLevel = "warn";
    kallisto::Logger::getInstance().setup(config);
    
    std::cout << "=== Kallisto Multi-threaded Benchmark ===\n";
    std::cout << "Workers: " << NUM_WORKERS << "\n";
    std::cout << "Total Items: " << TOTAL_ITEMS << "\n";
    std::cout << "Items per Worker: " << ITEMS_PER_WORKER << "\n\n";
    
    // Create shared table (capacity for ~25% load factor)
    kallisto::CuckooTable table(2000000);
    
    // Counters
    std::atomic<uint64_t> total_writes{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> total_hits{0};
    
    // Pre-generate data for each worker
    std::cout << "[SETUP] Pre-generating data for " << NUM_WORKERS << " workers...\n";
    std::vector<std::vector<std::string>> worker_keys(NUM_WORKERS);
    std::vector<std::vector<kallisto::SecretEntry>> worker_entries(NUM_WORKERS);
    
    for (size_t w = 0; w < NUM_WORKERS; ++w) {
        worker_keys[w].reserve(ITEMS_PER_WORKER);
        worker_entries[w].reserve(ITEMS_PER_WORKER);
        
        for (size_t i = 0; i < ITEMS_PER_WORKER; ++i) {
            std::string key = "w" + std::to_string(w) + "_k" + std::to_string(i);
            
            kallisto::SecretEntry entry;
            entry.key = key;
            entry.value = "v" + std::to_string(i);
            entry.path = "/bench/worker" + std::to_string(w);
            
            worker_keys[w].push_back(key);
            worker_entries[w].push_back(entry);
        }
    }
    std::cout << "[SETUP] Data generation complete.\n\n";
    
    // Create worker pool
    auto pool = kallisto::createWorkerPool(NUM_WORKERS);
    
    // Use std::latch for synchronization (C++20)
    std::latch work_done(NUM_WORKERS);
    
    // Start workers
    pool->start([&]() {
        std::cout << "[WORKERS] All " << NUM_WORKERS << " workers ready.\n";
    });
    
    std::cout << "[RUN] Starting benchmark...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    // Post benchmark work to each worker
    for (size_t w = 0; w < NUM_WORKERS; ++w) {
        pool->getWorker(w).dispatcher().post([
            &table, &worker_keys, &worker_entries,
            &total_writes, &total_reads, &total_hits,
            &work_done, w, ITEMS_PER_WORKER
        ]() {
            auto& keys = worker_keys[w];
            auto& entries = worker_entries[w];
            
            // Phase 1: Writes
            for (size_t i = 0; i < ITEMS_PER_WORKER; ++i) {
                table.insert(keys[i], entries[i]);
                total_writes.fetch_add(1, std::memory_order_relaxed);
            }
            
            // Phase 2: Reads
            for (size_t i = 0; i < ITEMS_PER_WORKER; ++i) {
                auto result = table.lookup(keys[i]);
                total_reads.fetch_add(1, std::memory_order_relaxed);
                if (result.has_value()) {
                    total_hits.fetch_add(1, std::memory_order_relaxed);
                }
            }
            
            // Signal completion
            work_done.count_down();
        });
    }
    
    // Wait for all workers to complete
    work_done.wait();
    
    auto end = std::chrono::high_resolution_clock::now();
    
    // Calculate results
    std::chrono::duration<double> elapsed = end - start;
    
    uint64_t writes = total_writes.load();
    uint64_t reads = total_reads.load();
    uint64_t hits = total_hits.load();
    
    double write_rps = writes / elapsed.count();
    double read_rps = reads / elapsed.count();
    double total_rps = (writes + reads) / elapsed.count();
    
    // Stop workers
    pool->stop();
    
    // Report
    std::cout << "\n=== RESULTS ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total Time     : " << elapsed.count() << " s\n";
    std::cout << "Write Operations: " << writes << "\n";
    std::cout << "Read Operations : " << reads << "\n";
    std::cout << "Hit Rate       : " << (100.0 * hits / reads) << "%\n";
    std::cout << "\n";
    std::cout << "Write RPS      : " << std::fixed << std::setprecision(0) << write_rps << " req/s\n";
    std::cout << "Read RPS       : " << read_rps << " req/s\n";
    std::cout << "Total RPS      : " << total_rps << " req/s\n";
    std::cout << "\n";
    
    // Compare with single-threaded baseline
    std::cout << "=== COMPARISON (vs single-threaded baseline) ===\n";
    std::cout << "Single-thread Write: ~246,100 req/s\n";
    std::cout << "Single-thread Read : ~342,051 req/s\n";
    std::cout << "Multi-thread (" << NUM_WORKERS << "w) Write: " << write_rps << " req/s\n";
    std::cout << "Multi-thread (" << NUM_WORKERS << "w) Read : " << read_rps << " req/s\n";
    
    double write_speedup = write_rps / 246100.0;
    double read_speedup = read_rps / 342051.0;
    
    std::cout << "\nSpeedup (Write): " << std::setprecision(2) << write_speedup << "x\n";
    std::cout << "Speedup (Read) : " << read_speedup << "x\n";
    
    if (write_speedup > 1.5 || read_speedup > 1.5) {
        std::cout << "\n>> SUCCESS: Multi-threading shows significant improvement!\n";
    } else if (write_speedup > 0.8 && read_speedup > 0.8) {
        std::cout << "\n>> OK: Performance maintained with multi-threading.\n";
        std::cout << "   Note: Shared CuckooTable uses locks, limiting parallel speedup.\n";
    } else {
        std::cout << "\n>> NOTE: Contention limits parallel speedup for shared data.\n";
    }
    
    return 0;
}
