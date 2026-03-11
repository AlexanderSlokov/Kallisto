#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include "kallisto/rocksdb_storage.hpp"
#include "kallisto/logger.hpp"

// =========================================================================================
// TEST FRAMEWORK
// =========================================================================================
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
// ROCKSDB TESTS
// =========================================================================================

const std::string TEST_DB_PATH = "/tmp/kallisto_test_rocksdb";

void cleanup_test_db() {
    if (std::filesystem::exists(TEST_DB_PATH)) {
        std::filesystem::remove_all(TEST_DB_PATH);
    }
}

void test_rocksdb_crud() {
    std::cout << "\n--- Testing RocksDB Storage (CRUD) ---\n";
    cleanup_test_db();

    {
        kallisto::RocksDBStorage db(TEST_DB_PATH);
        ASSERT_TRUE(db.is_open(), "RocksDB should open successfully");

        // Scenario 1: Write and Read
        kallisto::SecretEntry entry1;
        entry1.path = "/test/path";
        entry1.key = "test_key";
        entry1.value = "test_value";
        entry1.created_at = std::chrono::time_point<std::chrono::system_clock>();

        ASSERT_TRUE(db.put(entry1.key, entry1), "Should insert secret successfully");

        auto retrieved = db.get("test_key");
        ASSERT_TRUE(retrieved.has_value(), "Should find the inserted secret");
        ASSERT_EQUAL(retrieved->value, "test_value", "Retrieved value should match");

        // Scenario 2: Non-existent key
        auto missing = db.get("missing_key");
        ASSERT_TRUE(!missing.has_value(), "Looking for non-existent key should return empty");

        // Scenario 3: Update existing key
        entry1.value = "new_value";
        // Update time logic can be mocked if necessary, ignoring created_time for this assertion
        ASSERT_TRUE(db.put(entry1.key, entry1), "Should update existing secret successfully");
        
        auto updated = db.get("test_key");
        ASSERT_TRUE(updated.has_value(), "Should find the updated secret");
        ASSERT_EQUAL(updated->value, "new_value", "Retrieved value should match updated value");

        // Scenario 4: Remove key
        ASSERT_TRUE(db.del("test_key"), "Should remove secret successfully");
        
        auto deleted = db.get("test_key");
        ASSERT_TRUE(!deleted.has_value(), "Key should be gone after deletion");
        
        // Scenario 5: Iterate all
        kallisto::SecretEntry entry2, entry3;
        entry2.path = "/test/path";
        entry2.key = "key2";
        entry2.value = "val2";
        db.put(entry2.key, entry2);
        
        entry3.path = "/test/path2";
        entry3.key = "key3";
        entry3.value = "val3";
        db.put(entry3.key, entry3);
        
        int count = 0;
        bool found_key2 = false;
        bool found_key3 = false;
        
        db.iterate_all([&](const kallisto::SecretEntry& entry) {
            count++;
            if (entry.key == "key2") found_key2 = true;
            if (entry.key == "key3") found_key3 = true;
        });
        
        ASSERT_EQUAL(count, 2, "Iterate all should find exactly 2 secrets");
        ASSERT_TRUE(found_key2, "Iterate all should find key2");
        ASSERT_TRUE(found_key3, "Iterate all should find key3");
    }
}

void test_rocksdb_durability() {
    std::cout << "\n--- Testing RocksDB Storage (Durability) ---\n";
    cleanup_test_db();
    
    // Phase 1: Write
    {
        kallisto::RocksDBStorage db(TEST_DB_PATH);
        ASSERT_TRUE(db.is_open(), "RocksDB should open successfully for writing");
        
        kallisto::SecretEntry entry;
        entry.path = "/durable/path";
        entry.key = "db_pass";
        entry.value = "super_secret_123";
        
        db.set_sync(true);
        ASSERT_TRUE(db.put(entry.key, entry), "Should write secret synchronously");
    } // DB goes out of scope and flushes
    
    // Phase 2: Read from Re-opened DB
    {
        kallisto::RocksDBStorage db_reopened(TEST_DB_PATH);
        ASSERT_TRUE(db_reopened.is_open(), "RocksDB should re-open successfully");
        
        auto retrieved = db_reopened.get("db_pass");
        ASSERT_TRUE(retrieved.has_value(), "Should find the secret after re-opening DB");
        ASSERT_EQUAL(retrieved->value, "super_secret_123", "Value should survive DB restart");
    }
    
    cleanup_test_db();
}

// =========================================================================================
// MAIN ENTRY POINT
// =========================================================================================
int main() {
    // Setup generic logger for tests
    kallisto::LogConfig config("test_log_rocksdb");
    config.logLevel = "error"; // Quiet mode
    kallisto::Logger::getInstance().setup(config);

    try {
        test_rocksdb_crud();
        test_rocksdb_durability();
        
        std::cout << "\n============================================\n";
        std::cout << "   ROCKSDB TESTS PASSED SUCCESSFULLY 🚀\n";
        std::cout << "============================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception during tests: " << e.what() << std::endl;
        return 1;
    }
}
