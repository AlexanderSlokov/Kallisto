#include <gtest/gtest.h>

#include "kallisto/rocksdb_storage.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace kallisto;

// =============================================================================
// ROCKSDB STORAGE TEST SUITE
//
// Problem Description:
//   RocksDBStorage is the persistence layer for Kallisto. It backs up
//   the in-memory CuckooTable to disk via WAL for crash recovery.
//   Failures here mean data loss, silent corruption, or inability to
//   recover after power loss.
//
// Coverage Strategy:
//   1. Basic CRUD: put, get, del — each tested independently (SRP)
//   2. Durability: write → close DB → reopen → verify data survives
//   3. iterate_all: full scan, empty DB, partial deletions
//   4. Sync modes: IMMEDIATE vs BATCH toggle
//   5. Flush: explicit WAL flush behavior
//   6. Boundary values: empty keys, empty values, large payloads
//   7. Concurrency: multi-threaded put/get safety (RocksDB is internally thread-safe)
//   8. Error paths: get non-existent key, del non-existent key, double-delete
// =============================================================================

static const std::string test_db_path = "/tmp/kallisto_test_rocksdb";

class RocksDBStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        cleanupTestDatabase();
        storage_ = std::make_unique<RocksDBStorage>(test_db_path);
    }

    void TearDown() override {
        storage_.reset(); // Close DB before removing files
        cleanupTestDatabase();
    }

    void cleanupTestDatabase() {
        if (std::filesystem::exists(test_db_path)) {
            std::filesystem::remove_all(test_db_path);
        }
    }

    SecretEntry makeEntry(const std::string& key, const std::string& value,
                          const std::string& path = "/test") {
        SecretEntry entry;
        entry.key = key;
        entry.value = value;
        entry.path = path;
        entry.created_at = std::chrono::system_clock::now();
        entry.ttl = 3600;
        return entry;
    }

    std::unique_ptr<RocksDBStorage> storage_;
};

// ---------------------------------------------------------------------------
// 1. Database Lifecycle
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, OpensSuccessfully) {
    EXPECT_TRUE(storage_->is_open());
}

TEST_F(RocksDBStorageTest, CreatesDirectoryIfMissing) {
    storage_.reset();
    cleanupTestDatabase();

    std::string nested_path = test_db_path + "/nested/deep/dir";
    RocksDBStorage nested_db(nested_path);
    EXPECT_TRUE(nested_db.is_open());
}

// ---------------------------------------------------------------------------
// 2. Basic CRUD — each operation tested in isolation
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, PutAndGetSingleEntry) {
    auto entry = makeEntry("db_password", "s3cret!", "/prod/db");
    EXPECT_TRUE(storage_->put("db_password", entry));

    auto result = storage_->get("db_password");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->key, "db_password");
    EXPECT_EQ(result->value, "s3cret!");
    EXPECT_EQ(result->path, "/prod/db");
    EXPECT_EQ(result->ttl, 3600);
}

TEST_F(RocksDBStorageTest, GetNonExistentKeyReturnsNullopt) {
    auto result = storage_->get("ghost_key");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RocksDBStorageTest, UpdateExistingKey) {
    storage_->put("api_key", makeEntry("api_key", "v1_old"));
    storage_->put("api_key", makeEntry("api_key", "v2_new"));

    auto result = storage_->get("api_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "v2_new");
}

TEST_F(RocksDBStorageTest, DeleteExistingKey) {
    storage_->put("temp_key", makeEntry("temp_key", "temp_val"));
    EXPECT_TRUE(storage_->del("temp_key"));

    auto result = storage_->get("temp_key");
    EXPECT_FALSE(result.has_value());
}

TEST_F(RocksDBStorageTest, DeleteNonExistentKeySucceeds) {
    // RocksDB Delete is idempotent — deleting a missing key is not an error
    EXPECT_TRUE(storage_->del("never_existed"));
}

TEST_F(RocksDBStorageTest, DoubleDeleteSucceeds) {
    storage_->put("once", makeEntry("once", "val"));
    EXPECT_TRUE(storage_->del("once"));
    EXPECT_TRUE(storage_->del("once")); // Second delete should also succeed
}

// ---------------------------------------------------------------------------
// 3. iterate_all
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, IterateAllReturnsAllEntries) {
    storage_->put("k1", makeEntry("k1", "v1"));
    storage_->put("k2", makeEntry("k2", "v2"));
    storage_->put("k3", makeEntry("k3", "v3"));

    std::vector<std::string> found_keys;
    storage_->iterate_all([&](const SecretEntry& entry) {
        found_keys.push_back(entry.key);
    });

    EXPECT_EQ(found_keys.size(), 3);
}

TEST_F(RocksDBStorageTest, IterateAllOnEmptyDatabase) {
    int count = 0;
    storage_->iterate_all([&](const SecretEntry&) { count++; });
    EXPECT_EQ(count, 0);
}

TEST_F(RocksDBStorageTest, IterateAllExcludesDeletedKeys) {
    storage_->put("keep", makeEntry("keep", "yes"));
    storage_->put("drop", makeEntry("drop", "no"));
    storage_->del("drop");

    std::vector<std::string> found_keys;
    storage_->iterate_all([&](const SecretEntry& entry) {
        found_keys.push_back(entry.key);
    });

    EXPECT_EQ(found_keys.size(), 1);
    EXPECT_EQ(found_keys[0], "keep");
}

// ---------------------------------------------------------------------------
// 4. Durability — Crash/Power Loss simulation
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, DataSurvivesReopen) {
    // Phase 1: Write with sync mode
    storage_->set_sync(true);
    storage_->put("durable_key", makeEntry("durable_key", "durable_value", "/vault"));
    storage_.reset(); // Close DB (triggers flush in destructor)

    // Phase 2: Reopen and verify
    auto reopened = std::make_unique<RocksDBStorage>(test_db_path);
    ASSERT_TRUE(reopened->is_open());

    auto result = reopened->get("durable_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "durable_value");
    EXPECT_EQ(result->path, "/vault");
}

TEST_F(RocksDBStorageTest, MultipleEntriesSurviveReopen) {
    storage_->set_sync(true);
    for (int i = 0; i < 100; ++i) {
        std::string key = "persist_" + std::to_string(i);
        storage_->put(key, makeEntry(key, "val_" + std::to_string(i)));
    }
    storage_.reset();

    auto reopened = std::make_unique<RocksDBStorage>(test_db_path);
    ASSERT_TRUE(reopened->is_open());

    // Verify random samples
    for (int sample : {0, 25, 50, 75, 99}) {
        std::string key = "persist_" + std::to_string(sample);
        auto result = reopened->get(key);
        ASSERT_TRUE(result.has_value()) << "Key '" << key << "' should survive reopen";
        EXPECT_EQ(result->value, "val_" + std::to_string(sample));
    }
}

// ---------------------------------------------------------------------------
// 5. Sync Mode & Flush
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, SetSyncToggle) {
    // Should not throw or crash
    EXPECT_NO_THROW(storage_->set_sync(true));
    EXPECT_NO_THROW(storage_->set_sync(false));
}

TEST_F(RocksDBStorageTest, ExplicitFlush) {
    storage_->put("flush_test", makeEntry("flush_test", "flushed"));
    EXPECT_NO_THROW(storage_->flush());

    // Data should still be readable after flush
    auto result = storage_->get("flush_test");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "flushed");
}

// ---------------------------------------------------------------------------
// 6. Boundary Value Testing
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, EmptyKeyAndValue) {
    auto entry = makeEntry("", "", "");
    EXPECT_TRUE(storage_->put("", entry));

    auto result = storage_->get("");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "");
    EXPECT_EQ(result->path, "");
}

TEST_F(RocksDBStorageTest, LargePayload) {
    // 1MB value — stress test serialization and RocksDB block handling
    std::string large_value(1024 * 1024, 'X');
    auto entry = makeEntry("big_key", large_value, "/big");
    EXPECT_TRUE(storage_->put("big_key", entry));

    auto result = storage_->get("big_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value.size(), 1024 * 1024);
    EXPECT_EQ(result->value, large_value);
}

TEST_F(RocksDBStorageTest, TimestampAndTtlSerialization) {
    auto now = std::chrono::system_clock::now();

    SecretEntry entry;
    entry.key = "ts_key";
    entry.value = "ts_val";
    entry.path = "/ts";
    entry.created_at = now;
    entry.ttl = 86400; // 24 hours

    storage_->put("ts_key", entry);

    auto result = storage_->get("ts_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ttl, 86400);

    // Timestamps should match within 1 second (time_t precision)
    auto expected_time_t = std::chrono::system_clock::to_time_t(now);
    auto actual_time_t = std::chrono::system_clock::to_time_t(result->created_at);
    EXPECT_EQ(expected_time_t, actual_time_t);
}

// ---------------------------------------------------------------------------
// 7. Concurrency
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, ConcurrentPutAndGet) {
    // Problem: Multiple threads writing and reading simultaneously.
    // RocksDB is internally thread-safe — this validates no crashes occur.
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 200;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                storage_->put(key, makeEntry(key, "v"));
                storage_->get(key);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Verify a sample key from each thread
    for (int t = 0; t < num_threads; ++t) {
        std::string key = "t" + std::to_string(t) + "_k0";
        auto result = storage_->get(key);
        ASSERT_TRUE(result.has_value()) << "Key from thread " << t << " not found";
    }
}

// ---------------------------------------------------------------------------
// 8. Stress Test — many entries
// ---------------------------------------------------------------------------

TEST_F(RocksDBStorageTest, BulkInsertAndIterateAll) {
    constexpr int num_entries = 1000;

    for (int i = 0; i < num_entries; ++i) {
        std::string key = "bulk_" + std::to_string(i);
        storage_->put(key, makeEntry(key, "v" + std::to_string(i)));
    }

    int count = 0;
    storage_->iterate_all([&](const SecretEntry&) { count++; });
    EXPECT_EQ(count, num_entries);
}
