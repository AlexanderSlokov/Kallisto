/*
 * Source: tests/bench_p99.cpp
 * Purpose: Measure p99 Latency of Kallisto ShardedCuckooTable (64 shards)
 * Origin: Built from scratch for Report Requirement
 * Updated: 2025-01-18 - ShardedCuckooTable integration
 */

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include "kallisto/sharded_cuckoo_table.hpp"  // Updated for sharding
#include "kallisto/siphash.hpp"

// Utility to generate random string
std::string random_string(size_t length) {
    auto randomchar = []() -> char {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
    };
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randomchar);
    return str;
}

int main() {
    std::cout << "=== Kallisto Benchmark: p99 Latency (ShardedCuckooTable) ===\n";
    std::cout << "Shards: 64\n\n";
    
    // 1. Setup
    const int ITEM_COUNT = 1000000;
    kallisto::ShardedCuckooTable table(2000000);  // 2M capacity across 64 shards
    std::vector<std::string> keys;
    std::vector<std::string> values;

    std::cout << "[SETUP] Generating " << ITEM_COUNT << " items...\n";
    for (int i = 0; i < ITEM_COUNT; ++i) {
        std::string k = "key_" + std::to_string(i) + "_" + random_string(8);
        std::string v = "val_" + std::to_string(i);
        keys.push_back(k);
        values.push_back(v);
        
        kallisto::SecretEntry entry;
        entry.key = k;
        entry.value = v;
        table.insert(k, entry);
    }
    std::cout << "[SETUP] Insert complete.\n";

    // 2. Measure Latency
    std::vector<double> latencies; // micro-seconds
    latencies.reserve(ITEM_COUNT);

    std::cout << "[RUN] Performing 10,000 Lookups...\n";
    auto total_start = std::chrono::high_resolution_clock::now();

    for (const auto& k : keys) {
        auto t1 = std::chrono::high_resolution_clock::now();
        auto result = table.lookup(k);
        auto t2 = std::chrono::high_resolution_clock::now();

        if (!result) {
            std::cerr << "Error: Key not found! Logic bug?\n";
            return 1;
        }

        std::chrono::duration<double, std::micro> ms = t2 - t1;
        latencies.push_back(ms.count());
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();

    // 3. Analyze
    std::sort(latencies.begin(), latencies.end());
    size_t p99_idx = static_cast<size_t>(ITEM_COUNT * 0.99);
    double p99_val = latencies[p99_idx];
    
    double sum = 0;
    for(double d : latencies) sum += d;
    double avg = sum / ITEM_COUNT;

    std::chrono::duration<double, std::milli> total_ms = total_end - total_start;

    // 4. Report
    std::cout << "\n=== RESULTS ===\n";
    std::cout << "Total Runtime: " << total_ms.count() << " ms\n";
    std::cout << "Average Latency: " << avg << " us\n";
    std::cout << "p99 Latency: " << p99_val << " us (" << (p99_val / 1000.0) << " ms)\n";
    
    if (p99_val < 1000.0) {
        std::cout << ">> PASS: p99 < 1ms requirement met.\n";
    } else {
        std::cout << ">> FAIL: p99 > 1ms.\n";
    }

    return 0;
}

