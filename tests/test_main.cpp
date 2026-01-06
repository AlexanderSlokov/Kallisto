#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include "kallisto/siphash.hpp"
#include "kallisto/cuckoo_table.hpp"
#include "kallisto/logger.hpp"

// =========================================================================================
// TEST FRAMEWORK
// =========================================================================================
// Why a custom minimal test runner?
// We want to avoid heavy dependencies like GTest for this MVP phase.
// A simple macro-based assertion system is sufficient to prove correctness 
// without complicating the build process (Keep It Simple, Stupid).
#define ASSERT_TRUE(condition, message) \
    if (!(condition)) { \
        std::cerr << "❌ FAIL: " << message << std::endl; \
        std::exit(1); \
    } else { \
        std::cout << "✅ PASS: " << message << std::endl; \
    }

#define ASSERT_EQUAL(a, b, message) \
    if ((a) != (b)) { \
        std::cerr << "❌ FAIL: " << message << " [Expected: " << (a) << ", Got: " << (b) << "]" << std::endl; \
        std::exit(1); \
    } else { \
        std::cout << "✅ PASS: " << message << std::endl; \
    }

// =========================================================================================
// SIPHASH TESTS
// =========================================================================================
void test_siphash() {
    std::cout << "\n--- Testing SipHash ---\n";

    // Scenario: Consistency Check
    // Why? A hash function must be deterministic. Same input + same key = same output.
    // If this fails, the entire storage lookup mechanism breaks.
    std::string input = "test_key";
    uint64_t hash1 = kallisto::SipHash::hash(input, 123, 456);
    uint64_t hash2 = kallisto::SipHash::hash(input, 123, 456);
    ASSERT_EQUAL(hash1, hash2, "SipHash must be deterministic");

    // Scenario: Avalanche Effect
    // Why? A good hash function should change significantly if a single bit changes.
    // This prevents "Hash Flooding" attacks where attackers predict collisions.
    std::string input_modified = "test_key_"; 
    uint64_t hash3 = kallisto::SipHash::hash(input_modified, 123, 456);
    ASSERT_TRUE(hash1 != hash3, "SipHash should produce different hashes for slightly different inputs");
}

// =========================================================================================
// CUCKOO TABLE TESTS
// =========================================================================================
void test_cuckoo_table() {
    std::cout << "\n--- Testing Cuckoo Table ---\n";

    // Initialize with a small size to force collisions potentially later
    kallisto::CuckooTable table(1024);

    // Prepare test data
    kallisto::SecretEntry entry1;
    entry1.key = "db_pass";
    entry1.value = "secret123";
    entry1.path = "/prod";
    
    // Scenario 1: Basic Insert & Lookup
    // Why? The most fundamental requirement. Can we get back what we put in?
    bool inserted = table.insert("key1", entry1);
    ASSERT_TRUE(inserted, "Should successfully insert a new key");

    auto result = table.lookup("key1");
    ASSERT_TRUE(result.has_value(), "Should find the inserted key");
    ASSERT_EQUAL(result->value, "secret123", "Value retrieved must match inserted value");

    // Scenario 2: Update Existing Key
    // Why? KV Stores must act like a map. Inserting same key again should update value,
    // not create duplicates or fail silently.
    entry1.value = "new_secret";
    table.insert("key1", entry1);
    auto result_updated = table.lookup("key1");
    ASSERT_EQUAL(result_updated->value, "new_secret", "Subsequent insert should update the value");

    // Scenario 3: Non-existent Key
    // Why? We must correctly handle misses without crashing or returning garbage.
    auto result_missing = table.lookup("ghost_key");
    ASSERT_TRUE(!result_missing.has_value(), "Lookup for non-existent key should return empty");

    // Scenario 4: Deletion
    // Why? Deletion in Cuckoo Hash is tricky (just marking slot empty).
    // We need to verify the slot is actually reusable or at least not findable.
    bool deleted = table.remove("key1");
    ASSERT_TRUE(deleted, "Remove should return true for existing key");
    
    auto result_deleted = table.lookup("key1");
    ASSERT_TRUE(!result_deleted.has_value(), "Key should be gone after deletion");

    // Scenario 5: Collision Handling (Simulated)
    // Why? Cuckoo Hash's main feature is evicting keys to alternate locations.
    // While hard to deterministically force a collision without knowing the internal seeds,
    // inserting multiple keys validates the probing logic doesn't crash.
    for(int i=0; i<100; ++i) {
        kallisto::SecretEntry e;
        e.value = std::to_string(i);
        table.insert("k_" + std::to_string(i), e);
    }
    
    // Verify one random key exists
    auto res_50 = table.lookup("k_50");
    ASSERT_EQUAL(res_50->value, "50", "Should retrieve keys from a populated table");
}

// =========================================================================================
// MAIN ENTRY POINT
// =========================================================================================
int main() {
    // Setup generic logger for tests
    kallisto::LogConfig config("test_log");
    config.logLevel = "error"; // Quiet mode
    kallisto::Logger::getInstance().setup(config);

    try {
        test_siphash();
        test_cuckoo_table();
        
        std::cout << "\n============================================\n";
        std::cout << "   ALL TESTS PASSED SUCCESSFULLY 🚀\n";
        std::cout << "============================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception during tests: " << e.what() << std::endl;
        return 1;
    }
}
