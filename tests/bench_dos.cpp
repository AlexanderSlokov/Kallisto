/*
 * Source: tests/bench_dos.cpp
 * Purpose: Security Proof (Hash Flooding & B-Tree Gate)
 * Origin: 
 *   - WeakCuckooTable: Copied from src/cuckoo_table.cpp (Core Logic) 
 *     but replaced SipHash with BadHash to simulate DoS attack.
 *   - BTree Logic: Uses src/btree_index.cpp directly.
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cstring>
#include "kallisto/cuckoo_table.hpp"
#include "kallisto/btree_index.hpp"
#include "kallisto/siphash.hpp"

// ============================================
// PART 1: WEAK HASH IMPLEMENTATION (SIMULATED)
// ============================================

// A "Weak Hash" function designed to cause collisions.
// It only returns the length of the string, 
// meaning all strings of same length collide.
size_t bad_hash(const std::string& key) {
    return key.length(); 
}

// COPIED FROM src/cuckoo_table.cpp
// Test "What if we didn't use SipHash?"
// We need a version of the table that uses the bad_hash function above.
// We cannot modify the real class, so we mock it.
class WeakCuckooTable {
public:
    struct Bucket {
        bool occupied = false;
        std::string key;
    };

    std::vector<Bucket> table_1;
    std::vector<Bucket> table_2;
    size_t capacity;
    const int max_displacements = 100;

    WeakCuckooTable(size_t size) : capacity(size) {
        table_1.resize(capacity);
        table_2.resize(capacity);
    }

    // WEAK HASHING HERE
    size_t hash_1(const std::string& key) const { return bad_hash(key) % capacity; }
    size_t hash_2(const std::string& key) const { return (bad_hash(key) + 1) % capacity; } // Slightly different to avoid instant fail, but still weak

    // LOGIC COPIED FROM CuckooTable::insert
    bool insert(const std::string& key) {
        
	// (Simplified: Put only, no update check for benchmark speed)
        std::string current_key = key;
        
        for (int i = 0; i < max_displacements; ++i) {
            size_t h1 = hash_1(current_key);
            if (!table_1[h1].occupied) {
                table_1[h1] = {true, current_key};
                return true;
            }
            std::swap(current_key, table_1[h1].key);

            size_t h2 = hash_2(current_key);
            if (!table_2[h2].occupied) {
                table_2[h2] = {true, current_key};
                return true;
            }
            std::swap(current_key, table_2[h2].key);
        }
        return false; // Table full or cycle
    }
};

// ============================================
// PART 2: BENCHMARK LOGIC
// ============================================

void run_flooding_test() {
    std::cout << "\n[TEST] 1. Hash Flooding Resilience (SipHash vs WeakHash)\n";
    const int N = 5000; // WeakHash is VERY slow, 10000 is too much
    std::vector<std::string> attack_keys;
    
    // Generate colliding keys (All length 8 -> Same Hash in WeakTable)
    for(int i=0; i<N; ++i) {
        attack_keys.push_back(std::to_string(10000000 + i)); 
    }

    // A. WEAK SYSTEM
    {
        WeakCuckooTable weak_table(16384);
        auto start = std::chrono::high_resolution_clock::now();
        int success = 0;
        for(const auto& k : attack_keys) {
            if(weak_table.insert(k)) success++;
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        
        std::cout << "  > WEAK Hash (Simulated): " << elapsed.count() << " ms | Success: " << success << "/" << N << "\n";
        if (success < N) std::cout << "    (Many collisions caused insertion failure/cycles)\n";
    }

    // B. REAL SYSTEM (Kallisto)
    {
        kallisto::CuckooTable real_table(16384);
        auto start = std::chrono::high_resolution_clock::now();
        int success = 0;
        for(const auto& k : attack_keys) {
            kallisto::SecretEntry entry;
            entry.key = k;
            if(real_table.insert(k, entry)) success++;
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        std::cout << "  > SIPHASH (Kallisto)   : " << elapsed.count() << " ms | Success: " << success << "/" << N << "\n";
    }
}

void run_btree_gate_test() {
    std::cout << "\n[TEST] 2. B-Tree Gate Efficiency (Invalid Path Rejection)\n";
    const int N = 10000;
    
    // Setup: Valid B-Tree
    kallisto::BTreeIndex btree(5);
    for(int i=0; i<100; ++i) btree.insert_path("/valid/path/" + std::to_string(i));

    // Attack: 10,000 requests to INVALID paths
    std::vector<std::string> invalid_paths;
    for(int i=0; i<N; ++i) invalid_paths.push_back("/hack/attempt/" + std::to_string(i));

    auto start = std::chrono::high_resolution_clock::now();
    int blocked = 0;
    for(const auto& p : invalid_paths) {
        if(!btree.validate_path(p)) blocked++;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << "  > Processed " << N << " invalid requests in " << elapsed.count() << " ms.\n";
    std::cout << "  > Block rate: " << blocked << "/" << N << " (Should be 100%)\n";
    std::cout << "  > Avg Latency per Block: " << (elapsed.count() * 1000 / N) << " us\n";
}

int main() {
    std::cout << "=== Kallisto Security Benchmark ===\n";
    run_flooding_test();
    run_btree_gate_test();
    return 0;
}
