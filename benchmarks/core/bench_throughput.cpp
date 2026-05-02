#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include "kallisto/cuckoo_table.hpp"
int main() {
    const int N = 1000000;
    std::cout << "=== Kallisto Benchmark: Throughput (Batch Mode) ===\n";
    std::cout << "[CONFIG] Operations: " << N << " Inserts\n";
    // 1. Prepare Data
    std::vector<std::string> keys;
    std::vector<kallisto::SecretEntry> entries;
    keys.reserve(N);
    entries.reserve(N);
    std::cout << "[SETUP] Generating data...\n";
    for(int i=0; i<N; ++i) {
        std::string k = "key_" + std::to_string(i);
        keys.push_back(k);
        kallisto::SecretEntry e;
        e.key = k;
        e.value = "val_" + std::to_string(i);
        entries.push_back(e);
    }
    // 2. Measure Insert Throughput
    kallisto::CuckooTable table(2000000); // Reduce resize noise to measure raw insert speed
    
    std::cout << "[RUN] Inserting " << N << " items...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    int success_count = 0;
    for(int i=0; i<N; ++i) {
        if(table.insert(keys[i], entries[i])) {
            success_count++;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> seconds = end - start;
    // 3. Report
    double ops_per_sec = N / seconds.count();
    
    std::cout << "\n=== RESULTS ===\n";
    std::cout << "Time Elapsed: " << seconds.count() << " s\n";
    std::cout << "Success: " << success_count << "/" << N << "\n";
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << ops_per_sec << " ops/sec\n";
    
    return 0;
}
