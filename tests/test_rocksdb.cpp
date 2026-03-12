#include <string>
#include <vector>
#include <filesystem>
#include <gtest/gtest.h>
#include "kallisto/rocksdb_storage.hpp"

// =========================================================================================
// ROCKSDB TESTS
// =========================================================================================

const std::string TEST_DB_PATH = "/tmp/kallisto_test_rocksdb";

class RocksDBTest : public ::testing::Test {
protected:
    void SetUp() override {
        cleanup_test_db();
    }

    void TearDown() override {
        cleanup_test_db();
    }

    void cleanup_test_db() {
        if (std::filesystem::exists(TEST_DB_PATH)) {
            std::filesystem::remove_all(TEST_DB_PATH);
        }
    }
};


TEST_F(RocksDBTest, CrudOperations) {
    kallisto::RocksDBStorage db(TEST_DB_PATH);
    ASSERT_TRUE(db.is_open()) << "RocksDB should open successfully";

    // Scenario 1: Write and Read
    kallisto::SecretEntry entry1;
    entry1.path = "/test/path";
    entry1.key = "test_key";
    entry1.value = "test_value";
    entry1.created_at = std::chrono::time_point<std::chrono::system_clock>();

    EXPECT_TRUE(db.put(entry1.key, entry1)) << "Should insert secret successfully";

    auto retrieved = db.get("test_key");
    ASSERT_TRUE(retrieved.has_value()) << "Should find the inserted secret";
    EXPECT_EQ(retrieved->value, "test_value") << "Retrieved value should match";

    // Scenario 2: Non-existent key
    auto missing = db.get("missing_key");
    EXPECT_FALSE(missing.has_value()) << "Looking for non-existent key should return empty";

    // Scenario 3: Update existing key
    entry1.value = "new_value";
    EXPECT_TRUE(db.put(entry1.key, entry1)) << "Should update existing secret successfully";
    
    auto updated = db.get("test_key");
    ASSERT_TRUE(updated.has_value()) << "Should find the updated secret";
    EXPECT_EQ(updated->value, "new_value") << "Retrieved value should match updated value";

    // Scenario 4: Remove key
    EXPECT_TRUE(db.del("test_key")) << "Should remove secret successfully";
    
    auto deleted = db.get("test_key");
    EXPECT_FALSE(deleted.has_value()) << "Key should be gone after deletion";
    
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
    
    EXPECT_EQ(count, 2) << "Iterate all should find exactly 2 secrets";
    EXPECT_TRUE(found_key2) << "Iterate all should find key2";
    EXPECT_TRUE(found_key3) << "Iterate all should find key3";
}

TEST_F(RocksDBTest, Durability) {
    // Phase 1: Write
    {
        kallisto::RocksDBStorage db(TEST_DB_PATH);
        ASSERT_TRUE(db.is_open()) << "RocksDB should open successfully for writing";
        
        kallisto::SecretEntry entry;
        entry.path = "/durable/path";
        entry.key = "db_pass";
        entry.value = "super_secret_123";
        
        db.set_sync(true);
        EXPECT_TRUE(db.put(entry.key, entry)) << "Should write secret synchronously";
    } // DB goes out of scope and flushes
    
    // Phase 2: Read from Re-opened DB
    {
        kallisto::RocksDBStorage db_reopened(TEST_DB_PATH);
        ASSERT_TRUE(db_reopened.is_open()) << "RocksDB should re-open successfully";
        
        auto retrieved = db_reopened.get("db_pass");
        ASSERT_TRUE(retrieved.has_value()) << "Should find the secret after re-opening DB";
        EXPECT_EQ(retrieved->value, "super_secret_123") << "Value should survive DB restart";
    }
}
