/*
 * Source: tests/bench_multithread.cpp
 * Purpose: Comprehensive multi-threaded benchmark suite for ShardedCuckooTable
 * 
 * Benchmark Patterns (Simulating Real Vault Server Workload):
 *   1. MIXED:  95% reads, 5% writes (steady-state production)
 *   2. ZIPF:   Hot keys distribution (20% keys get 80% traffic)
 *   3. BURSTY: Deployment bursts (pods startup, fetch secrets)
 *   4. ALL:    Combined realistic workload
 */

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <thread>
#include <latch>
#include <cmath>

#include "kallisto/event/worker.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {
    event::WorkerPoolPtr createWorkerPool(size_t num_workers);
}

// =============================================================================
// BENCHMARK UTILITIES
// =============================================================================

struct BenchResult {
    double elapsed_sec;
    uint64_t total_ops;
    uint64_t reads;
    uint64_t writes;
    uint64_t hits;
    
    double ops_per_sec() const { return total_ops / elapsed_sec; }
    double read_rps() const { return reads / elapsed_sec; }
    double write_rps() const { return writes / elapsed_sec; }
    double hit_rate() const { return reads > 0 ? 100.0 * hits / reads : 0; }
};

void print_result(const std::string& name, const BenchResult& r) {
    std::cout << "\n--- " << name << " ---\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Time: " << r.elapsed_sec << "s | Total Ops: " << r.total_ops << "\n";
    std::cout << "Reads: " << r.reads << " | Writes: " << r.writes << "\n";
    std::cout << "Hit Rate: " << r.hit_rate() << "%\n";
    std::cout << std::setprecision(0);
    std::cout << "Total RPS: " << r.ops_per_sec() << " req/s\n";
    std::cout << "Read RPS:  " << r.read_rps() << " req/s\n";
    std::cout << "Write RPS: " << r.write_rps() << " req/s\n";
}

// Zipf distribution generator (for hot keys simulation)
class ZipfGenerator {
public:
    ZipfGenerator(size_t n, double skew = 1.0) : n_(n), skew_(skew) {
        // Precompute normalization constant
        for (size_t i = 1; i <= n; ++i) {
            norm_ += 1.0 / std::pow(i, skew);
        }
    }
    
    size_t next(std::mt19937& rng) {
        std::uniform_real_distribution<> dist(0.0, 1.0);
        double u = dist(rng);
        double sum = 0;
        for (size_t i = 1; i <= n_; ++i) {
            sum += 1.0 / (std::pow(i, skew_) * norm_);
            if (sum >= u) return i - 1;
        }
        return n_ - 1;
    }
    
private:
    size_t n_;
    double skew_;
    double norm_ = 0;
};

// =============================================================================
// BENCHMARK 1: MIXED WORKLOAD (95% read, 5% write)
// =============================================================================

BenchResult bench_mixed(kallisto::ShardedCuckooTable& table,
                        const std::vector<std::string>& keys,
                        const std::vector<kallisto::SecretEntry>& entries,
                        size_t num_workers, size_t ops_per_worker) {
    
    std::atomic<uint64_t> total_reads{0}, total_writes{0}, total_hits{0};
    std::latch done(num_workers);
    
    auto pool = kallisto::createWorkerPool(num_workers);
    pool->start([](){});
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t w = 0; w < num_workers; ++w) {
        pool->getWorker(w).dispatcher().post([&, w, ops_per_worker]() {
            std::mt19937 rng(w * 12345);
            std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);
            std::uniform_int_distribution<int> op_dist(0, 99);
            
            for (size_t i = 0; i < ops_per_worker; ++i) {
                size_t idx = key_dist(rng);
                
                if (op_dist(rng) < 5) {
                    // 5% writes
                    table.insert(keys[idx], entries[idx]);
                    total_writes.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // 95% reads
                    auto result = table.lookup(keys[idx]);
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                    if (result) total_hits.fetch_add(1, std::memory_order_relaxed);
                }
            }
            done.count_down();
        });
    }
    
    done.wait();
    auto end = std::chrono::high_resolution_clock::now();
    pool->stop();
    
    std::chrono::duration<double> elapsed = end - start;
    return {elapsed.count(), total_reads + total_writes, 
            total_reads.load(), total_writes.load(), total_hits.load()};
}

// =============================================================================
// BENCHMARK 2: ZIPF DISTRIBUTION (Hot Keys)
// =============================================================================

BenchResult bench_zipf(kallisto::ShardedCuckooTable& table,
                       const std::vector<std::string>& keys,
                       const std::vector<kallisto::SecretEntry>& entries,
                       size_t num_workers, size_t ops_per_worker) {
    
    std::atomic<uint64_t> total_reads{0}, total_writes{0}, total_hits{0};
    std::latch done(num_workers);
    
    auto pool = kallisto::createWorkerPool(num_workers);
    pool->start([](){});
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t w = 0; w < num_workers; ++w) {
        pool->getWorker(w).dispatcher().post([&, w, ops_per_worker]() {
            std::mt19937 rng(w * 54321);
            ZipfGenerator zipf(keys.size(), 1.2);  // Skew 1.2 for realistic hot keys
            std::uniform_int_distribution<int> op_dist(0, 99);
            
            for (size_t i = 0; i < ops_per_worker; ++i) {
                size_t idx = zipf.next(rng);
                
                if (op_dist(rng) < 5) {
                    table.insert(keys[idx], entries[idx]);
                    total_writes.fetch_add(1, std::memory_order_relaxed);
                } else {
                    auto result = table.lookup(keys[idx]);
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                    if (result) total_hits.fetch_add(1, std::memory_order_relaxed);
                }
            }
            done.count_down();
        });
    }
    
    done.wait();
    auto end = std::chrono::high_resolution_clock::now();
    pool->stop();
    
    std::chrono::duration<double> elapsed = end - start;
    return {elapsed.count(), total_reads + total_writes,
            total_reads.load(), total_writes.load(), total_hits.load()};
}

// =============================================================================
// BENCHMARK 3: BURSTY TRAFFIC (Deployment simulation)
// =============================================================================

BenchResult bench_bursty(kallisto::ShardedCuckooTable& table,
                         const std::vector<std::string>& keys,
                         const std::vector<kallisto::SecretEntry>& entries,
                         size_t num_workers, size_t bursts, size_t ops_per_burst) {
    
    std::atomic<uint64_t> total_reads{0}, total_writes{0}, total_hits{0};
    
    auto pool = kallisto::createWorkerPool(num_workers);
    pool->start([](){});
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t burst = 0; burst < bursts; ++burst) {
        std::latch burst_done(num_workers);
        
        // All workers burst-read simultaneously (simulating pods startup)
        for (size_t w = 0; w < num_workers; ++w) {
            pool->getWorker(w).dispatcher().post([&, w, burst, ops_per_burst]() {
                std::mt19937 rng(burst * 1000 + w);
                std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);
                std::uniform_int_distribution<int> op_dist(0, 99);
                
                // Burst phase: 100% reads (pods fetching startup secrets)
                size_t burst_reads = ops_per_burst / 2;
                for (size_t i = 0; i < burst_reads; ++i) {
                    size_t idx = key_dist(rng);
                    auto result = table.lookup(keys[idx]);
                    total_reads.fetch_add(1, std::memory_order_relaxed);
                    if (result) total_hits.fetch_add(1, std::memory_order_relaxed);
                }
                
                // Steady phase: 95/5 mixed
                size_t steady_ops = ops_per_burst / 2;
                for (size_t i = 0; i < steady_ops; ++i) {
                    size_t idx = key_dist(rng);
                    if (op_dist(rng) < 5) {
                        table.insert(keys[idx], entries[idx]);
                        total_writes.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        auto result = table.lookup(keys[idx]);
                        total_reads.fetch_add(1, std::memory_order_relaxed);
                        if (result) total_hits.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                
                burst_done.count_down();
            });
        }
        
        burst_done.wait();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    pool->stop();
    
    std::chrono::duration<double> elapsed = end - start;
    return {elapsed.count(), total_reads + total_writes,
            total_reads.load(), total_writes.load(), total_hits.load()};
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char** argv) {
    // Configuration
    const size_t NUM_WORKERS = 3;
    const size_t TOTAL_KEYS = 100000;      // 100K unique secrets
    const size_t OPS_PER_WORKER = 333333;  // ~1M total per benchmark
    const size_t NUM_BURSTS = 10;
    const size_t OPS_PER_BURST = 100000;
    
    // Setup logging
    kallisto::LogConfig config("bench_comprehensive");
    config.logLevel = "warn";
    kallisto::Logger::getInstance().setup(config);
    
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║   KALLISTO COMPREHENSIVE BENCHMARK SUITE (Vault Patterns)    ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Workers: " << NUM_WORKERS << "                                                    ║\n";
    std::cout << "║  Shards: 64                                                  ║\n";
    std::cout << "║  Keys: " << TOTAL_KEYS << "                                                ║\n";
    std::cout << "║  Ops/Worker: " << OPS_PER_WORKER << "                                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    // Pre-generate test data
    std::cout << "\n[SETUP] Generating " << TOTAL_KEYS << " keys...\n";
    std::vector<std::string> keys;
    std::vector<kallisto::SecretEntry> entries;
    keys.reserve(TOTAL_KEYS);
    entries.reserve(TOTAL_KEYS);
    
    for (size_t i = 0; i < TOTAL_KEYS; ++i) {
        keys.push_back("secret/app/key_" + std::to_string(i));
        kallisto::SecretEntry e;
        e.key = keys.back();
        e.value = "value_" + std::to_string(i);
        e.path = "/secret/app";
        entries.push_back(e);
    }
    
    // Create sharded table and pre-populate
    std::cout << "[SETUP] Creating ShardedCuckooTable (64 shards)...\n";
    kallisto::ShardedCuckooTable table(TOTAL_KEYS * 2);
    
    std::cout << "[SETUP] Pre-populating table with " << TOTAL_KEYS << " secrets...\n";
    for (size_t i = 0; i < TOTAL_KEYS; ++i) {
        table.insert(keys[i], entries[i]);
    }
    std::cout << "[SETUP] Done.\n";
    
    // ==========================================================================
    // RUN BENCHMARKS
    // ==========================================================================
    
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "BENCHMARK 1: MIXED WORKLOAD (95% read, 5% write)\n";
    std::cout << "Pattern: Typical production steady-state\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    auto r1 = bench_mixed(table, keys, entries, NUM_WORKERS, OPS_PER_WORKER);
    print_result("MIXED 95/5", r1);
    
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "BENCHMARK 2: ZIPF DISTRIBUTION (Hot Keys)\n";
    std::cout << "Pattern: 20% of keys receive 80% of traffic (Pareto)\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    auto r2 = bench_zipf(table, keys, entries, NUM_WORKERS, OPS_PER_WORKER);
    print_result("ZIPF HOT KEYS", r2);
    
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "BENCHMARK 3: BURSTY TRAFFIC (Deployment Simulation)\n";
    std::cout << "Pattern: " << NUM_BURSTS << " deployment bursts, pods startup fetch\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    auto r3 = bench_bursty(table, keys, entries, NUM_WORKERS, NUM_BURSTS, OPS_PER_BURST);
    print_result("BURSTY DEPLOYMENT", r3);
    
    // ==========================================================================
    // SUMMARY
    // ==========================================================================
    
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                     BENCHMARK SUMMARY                        ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "║  MIXED 95/5:        " << std::setw(10) << r1.ops_per_sec() << " RPS                       ║\n";
    std::cout << "║  ZIPF HOT KEYS:     " << std::setw(10) << r2.ops_per_sec() << " RPS                       ║\n";
    std::cout << "║  BURSTY DEPLOYMENT: " << std::setw(10) << r3.ops_per_sec() << " RPS                       ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    
    double avg_rps = (r1.ops_per_sec() + r2.ops_per_sec() + r3.ops_per_sec()) / 3;
    std::cout << "║  AVERAGE:           " << std::setw(10) << avg_rps << " RPS                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    // Compare with baseline
    double baseline_single = 294000;  // Approximate single-thread RPS
    double baseline_multi_old = 143000;  // Old multi-thread without sharding
    
    std::cout << "\n=== COMPARISON ===\n";
    std::cout << "Single-thread baseline:     ~" << baseline_single << " RPS\n";
    std::cout << "Multi-thread (no sharding): ~" << baseline_multi_old << " RPS\n";
    std::cout << "Multi-thread (64 shards):   ~" << avg_rps << " RPS\n";
    std::cout << "\nSharding improvement: " << std::setprecision(1) 
              << (avg_rps / baseline_multi_old) << "x vs non-sharded\n";
    
    if (avg_rps > baseline_single) {
        std::cout << "\n🚀 SUCCESS: Multi-threaded sharding exceeds single-thread!\n";
    } else if (avg_rps > baseline_multi_old * 1.5) {
        std::cout << "\n✅ GOOD: Significant improvement over non-sharded multi-thread.\n";
    } else {
        std::cout << "\n⚠️  Performance lower than expected. Check shard distribution.\n";
    }
    
    return 0;
}
