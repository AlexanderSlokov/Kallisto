/*
 * Problem Description:
 * TDD for KvEngine v2. Tests the new V2 interfaces using tl::expected and OCC.
 * We must test typical CRUD operations, edge cases (empty strings), and
 * simulate anomalous conditions like process crashes, I/O errors and race conditions.
 */
#include <gtest/gtest.h>
#include "kallisto/engine/kv_engine.hpp"
#include <filesystem>
#include <thread>
#include <vector>
#include <atomic>

using namespace kallisto::engine;

class KvEngineTestV2 : public ::testing::Test {
protected:
    std::string test_db_path = "/tmp/kallisto_kv_test_db_v2";

    void SetUp() override {
        std::filesystem::remove_all(test_db_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_db_path);
    }
};

TEST_F(KvEngineTestV2, BasicVersionedReadWrite) {
    // Problem Description: Verify happy-path CRUD operations and branch coverage for missing keys.
    auto engine = std::make_unique<KvEngine>(test_db_path);
    
    SecretPayload p1{"my_secret_pass", 3600};
    
    // Put version 1
    auto res_put = engine->put_version("app/db", p1);
    EXPECT_TRUE(res_put.has_value());
    
    // Read version 1
    auto res_read = engine->read_version("app/db", 1);
    ASSERT_TRUE(res_read.has_value());
    EXPECT_EQ(res_read->value, "my_secret_pass");
    EXPECT_EQ(res_read->ttl, 3600);
    
    // Read metadata
    auto meta = engine->read_metadata("app/db");
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->current_version, 1);
    EXPECT_EQ(meta->versions.size(), 1);
    EXPECT_EQ(meta->versions[0].version_id, 1);
    EXPECT_FALSE(meta->versions[0].destroyed);
    
    // Test branch: missing path
    auto miss_path = engine->read_version("app/db_wrong", 1);
    ASSERT_FALSE(miss_path.has_value());
    EXPECT_EQ(miss_path.error(), EngineError::NotFound);
    
    // Test branch: missing version
    auto miss_ver = engine->read_version("app/db", 99);
    ASSERT_FALSE(miss_ver.has_value());
    EXPECT_EQ(miss_ver.error(), EngineError::InvalidVersion);
}

TEST_F(KvEngineTestV2, SoftDeleteAndDestroy) {
    // Problem Description: Verify soft_delete marks it deleted but keeps payload, destroy wipes payload.
    auto engine = std::make_unique<KvEngine>(test_db_path);
    SecretPayload p1{"data", 0};
    ASSERT_TRUE(engine->put_version("app/data", p1).has_value());
    
    // Soft delete
    auto sd_res = engine->soft_delete("app/data", 1);
    EXPECT_TRUE(sd_res.has_value());
    
    // Read should return SoftDeleted
    auto read_sd = engine->read_version("app/data", 1);
    ASSERT_FALSE(read_sd.has_value());
    EXPECT_EQ(read_sd.error(), EngineError::SoftDeleted);
    
    // Destroy
    auto destroy_res = engine->destroy_version("app/data", 1);
    EXPECT_TRUE(destroy_res.has_value());
    
    auto read_destroy = engine->read_version("app/data", 1);
    ASSERT_FALSE(read_destroy.has_value());
    EXPECT_EQ(read_destroy.error(), EngineError::Destroyed);
    
    auto meta = engine->read_metadata("app/data");
    ASSERT_TRUE(meta.has_value());
    EXPECT_TRUE(meta->versions[0].destroyed);
}

TEST_F(KvEngineTestV2, OptimisticConcurrencyControl_CAS) {
    // Problem Description: Test CAS logic for put_version
    auto engine = std::make_unique<KvEngine>(test_db_path);
    SecretPayload p1{"v1", 0};
    ASSERT_TRUE(engine->put_version("cas/test", p1).has_value());
    
    SecretPayload p2{"v2", 0};
    // Expected CAS=1, provide CAS=1 -> Success
    auto res_cas_ok = engine->put_version("cas/test", p2, 1);
    EXPECT_TRUE(res_cas_ok.has_value());
    
    SecretPayload p3{"v3", 0};
    // Expected CAS=2, provide CAS=1 -> Mismatch
    auto res_cas_fail = engine->put_version("cas/test", p3, 1);
    ASSERT_FALSE(res_cas_fail.has_value());
    EXPECT_EQ(res_cas_fail.error(), EngineError::CasMismatch);
}

TEST_F(KvEngineTestV2, BoundaryValues) {
    // Problem Description: Test extreme/boundary values like empty path/value and TTL=0.
    auto engine = std::make_unique<KvEngine>(test_db_path);
    
    SecretPayload entry{"", 0};
    EXPECT_TRUE(engine->put_version("", entry).has_value());
    
    // read_version(path, 0) should return latest version
    auto retrieved = engine->read_version("", 0);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->value, "");
    EXPECT_EQ(retrieved->ttl, 0);
}

TEST_F(KvEngineTestV2, CrashRecoveryAndCacheMiss) {
    // Problem Description: Simulate sudden power loss / crash. The memory cache is wiped.
    {
        auto engine = std::make_unique<KvEngine>(test_db_path);
        engine->changeSyncMode(ISecretEngine::SyncMode::IMMEDIATE);
        
        SecretPayload entry{"crash_proof", 9999};
        ASSERT_TRUE(engine->put_version("sys/admin", entry).has_value());
    } 
    
    // Simulate restart
    auto engine_restarted = std::make_unique<KvEngine>(test_db_path);
    
    // Cache miss will happen here. It must pull from RocksDB.
    auto retrieved = engine_restarted->read_version("sys/admin", 1);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->value, "crash_proof");
}

TEST_F(KvEngineTestV2, ReadOnlyIOErrorSimulation) {
    // Problem Description: Simulate I/O write permission error (disk read-only)
    std::string read_only_dir = "/tmp/kallisto_readonly_test_v2";
    std::filesystem::create_directory(read_only_dir);
    std::filesystem::permissions(read_only_dir, 
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);
    
    auto engine = std::make_unique<KvEngine>(read_only_dir); 
    SecretPayload entry{"v1", 0};
    
    auto res = engine->put_version("fail/path", entry);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), EngineError::StorageError);

    std::filesystem::permissions(read_only_dir, std::filesystem::perms::all);
    std::filesystem::remove_all(read_only_dir);
}

TEST_F(KvEngineTestV2, ConcurrencyStressTest) {
    // Problem Description: Simulate high-concurrency race conditions to ensure thread safety.
    auto engine = std::make_unique<KvEngine>(test_db_path);
    constexpr int num_threads = 10;
    constexpr int ops_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                SecretPayload entry{"val_" + std::to_string(i) + "_" + std::to_string(j), 3600};
                std::string path = "concurrent/" + std::to_string(i);
                
                if (j % 100 == 0) {
                    auto mode = (j % 2 == 0) ? ISecretEngine::SyncMode::BATCH : ISecretEngine::SyncMode::IMMEDIATE;
                    engine->changeSyncMode(mode);
                }

                if (engine->put_version(path, entry).has_value()) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) { t.join(); }
    }

    EXPECT_EQ(success_count.load(), num_threads * ops_per_thread);
    // Version should be 1000 for each thread
    auto meta = engine->read_metadata("concurrent/5");
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->current_version, 1000);
}
