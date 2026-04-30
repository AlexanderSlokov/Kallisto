/*
 * Problem Description:
 * KvEngine manages the hot path cache, rocksdb persistence, and b-tree indexes.
 * We must test typical CRUD operations, edge cases (empty strings), and
 * simulate anomalous conditions like process crashes and synchronization thresholds.
 */
#include <gtest/gtest.h>
#include "kallisto/engine/kv_engine.hpp"
#include <filesystem>
#include <thread>
#include <vector>

using namespace kallisto::engine;

class KvEngineTest : public ::testing::Test {
protected:
    std::string test_db_path = "/tmp/kallisto_kv_test_db";

    void SetUp() override {
        std::filesystem::remove_all(test_db_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_db_path);
    }
};

TEST_F(KvEngineTest, BasicReadWriteAndDelete) {
    // Problem Description: Verify happy-path CRUD operations and branch coverage for missing keys.
    KvEngine engine(test_db_path);
    
    kallisto::SecretEntry entry;
    entry.path = "app/db";
    entry.key = "password";
    entry.value = "my_secret_pass";
    entry.ttl = 3600;
    
    EXPECT_TRUE(engine.put(entry));
    
    auto retrieved = engine.get("app/db", "password");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->value, "my_secret_pass");
    
    // Test branch: missing key
    EXPECT_FALSE(engine.get("app/db", "wrong_key").has_value());
    
    EXPECT_TRUE(engine.del("app/db", "password"));
    EXPECT_FALSE(engine.get("app/db", "password").has_value());
    // Deleting a non-existent key from RocksDB returns ok depending on API, but let's test it
    // Actually RocksDB Delete returns OK even if key doesn't exist, so this will return true.
    EXPECT_TRUE(engine.del("app/db", "password")); 
}

TEST_F(KvEngineTest, BoundaryValues) {
    // Problem Description: Test extreme/boundary values like empty path/key/value and TTL=0.
    KvEngine engine(test_db_path);
    
    kallisto::SecretEntry entry;
    entry.path = "";
    entry.key = "";
    entry.value = "";
    entry.ttl = 0;
    
    EXPECT_TRUE(engine.put(entry));
    
    auto retrieved = engine.get("", "");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->value, "");
    EXPECT_EQ(retrieved->ttl, 0);
}

TEST_F(KvEngineTest, CrashRecoveryAndCacheMiss) {
    // Problem Description: Simulate sudden power loss / crash. The memory cache is wiped.
    // The engine must rely on RocksDB fallback to serve the request.
    {
        KvEngine engine(test_db_path);
        engine.changeSyncMode(ISecretEngine::SyncMode::IMMEDIATE);
        
        kallisto::SecretEntry entry;
        entry.path = "sys/admin";
        entry.key = "token";
        entry.value = "crash_proof";
        entry.ttl = 9999;
        engine.put(entry);
    } // engine goes out of scope -> memory destroyed
    
    // Simulate restart
    KvEngine engine_restarted(test_db_path);
    
    // Cache miss will happen here. It must pull from RocksDB.
    auto retrieved = engine_restarted.get("sys/admin", "token");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->value, "crash_proof");
}

TEST_F(KvEngineTest, BatchModeSynchronization) {
    // Problem Description: Test if handleBatchSync() properly delays flushing 
    // until sync_threshold is hit or forceFlush is called.
    KvEngine engine(test_db_path);
    engine.changeSyncMode(ISecretEngine::SyncMode::BATCH);
    EXPECT_EQ(engine.getSyncMode(), ISecretEngine::SyncMode::BATCH);
    
    kallisto::SecretEntry entry;
    entry.path = "batch/test";
    entry.key = "k1";
    entry.value = "v1";
    
    // Write in batch mode, shouldn't hit disk immediately.
    EXPECT_TRUE(engine.put(entry));
    
    engine.forceFlush(); // Manual intervention
    
    auto retrieved = engine.get("batch/test", "k1");
    ASSERT_TRUE(retrieved.has_value());
}

TEST_F(KvEngineTest, ReadOnlyIOErrorSimulation) {
    // Problem Description: Simulate I/O write permission error (disk read-only)
    // by attempting to use a local directory with stripped write permissions.
    // Ensure the system fails gracefully instead of crashing.
    
    std::string read_only_dir = "/tmp/kallisto_readonly_test";
    std::filesystem::create_directory(read_only_dir);
    // Remove write permissions
    std::filesystem::permissions(read_only_dir, 
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);
    
    KvEngine engine(read_only_dir); 
    
    kallisto::SecretEntry entry;
    entry.path = "fail/path";
    entry.key = "k1";
    entry.value = "v1";
    
    // RocksDB put should fail gracefully and return false
    EXPECT_FALSE(engine.put(entry));

    // Cleanup: Restore permissions so the OS can delete it later
    std::filesystem::permissions(read_only_dir, std::filesystem::perms::all);
    std::filesystem::remove_all(read_only_dir);
}

TEST_F(KvEngineTest, ConcurrencyStressTest) {
    // Problem Description: Simulate high-concurrency race conditions.
    // 10 threads bombard the engine with simultaneous put() operations and sync_mode changes.
    KvEngine engine(test_db_path);
    constexpr int num_threads = 10;
    constexpr int ops_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                kallisto::SecretEntry entry;
                entry.path = "concurrent/" + std::to_string(i);
                entry.key = "key_" + std::to_string(j);
                entry.value = "val_" + std::to_string(i) + "_" + std::to_string(j);
                entry.ttl = 3600;
                
                if (j % 100 == 0) {
                    auto mode = (j % 2 == 0) ? ISecretEngine::SyncMode::BATCH : ISecretEngine::SyncMode::IMMEDIATE;
                    engine.changeSyncMode(mode);
                }

                if (engine.put(entry)) {
                    success_count++;
                }
                
                engine.getSyncMode();
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    EXPECT_EQ(success_count.load(), num_threads * ops_per_thread);
    auto res = engine.get("concurrent/5", "key_500");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, "val_5_500");
}
