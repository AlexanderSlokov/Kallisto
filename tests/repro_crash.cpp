#include "kallisto/cuckoo_table.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>

using namespace kallisto;

void stress_test_multithreaded() {
    std::cout << "Starting MULTI-THREADED stress test..." << std::endl;
    // Initiate with small capacity to force frequent reallocations
    CuckooTable table(1024, 100); 
    
    std::atomic<bool> running{true};
    std::atomic<size_t> total_inserts{0};

    // Writer Thread
    auto writer = std::thread([&]() {
        for (int i = 0; i < 100000; ++i) {
            std::string key = "key_" + std::to_string(i);
            SecretEntry entry;
            entry.key = key;
            entry.value = "value_" + std::to_string(i);
            
            table.insert(key, entry);
            total_inserts++;
            if (!running) break;
        }
        running = false;
    });

    // Reader Thread (Chaos Monkey requesting Stats)
    auto reader = std::thread([&]() {
        while (running) {
            // Hammer the stats function
            try {
                auto stats = table.get_memory_stats();
                volatile size_t s = stats.total_memory_allocated;
                (void)s;
            } catch (...) {
                // Ignore logic errors, we are looking for Segfaults
            }
            // Small yield to allow writer to progress but still be aggressive
            std::this_thread::yield(); 
        }
    });

    writer.join();
    reader.join();

    std::cout << "Multi-threaded test survived! (This is unexpected if code is not thread-safe)" << std::endl;
}

int main() {
    try {
        stress_test_multithreaded();
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
