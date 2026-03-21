#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <filesystem>
#include "kallisto/kallisto_engine.hpp"

using namespace kallisto;

// Test Fixture for KallistoEngine
class KallistoEngineTest : public ::testing::Test {
protected:
    std::string test_db_path = "/tmp/kallisto_test_123";

    void SetUp() override {
        std::filesystem::remove_all(test_db_path);
        engine = std::make_unique<KallistoEngine>(test_db_path);
    }

    void TearDown() override {
        engine.reset();
        std::filesystem::remove_all(test_db_path);
    }

    std::unique_ptr<KallistoEngine> engine;
};

// ============================================================================
// 1. Test Khởi tạo & Ghi/Đọc cơ bản
// ============================================================================
TEST_F(KallistoEngineTest, BasicReadWrite) {
    bool inserted = engine->put("/prod/db", "password", "super_secret");
    EXPECT_TRUE(inserted) << "Engine should successfully store the secret";

    auto entry = engine->get("/prod/db", "password");
    ASSERT_TRUE(entry.has_value()) << "Engine should retrieve the correct secret";
    EXPECT_EQ(entry->value, "super_secret") << "Engine should retrieve the correct secret value";

    bool deleted = engine->del("/prod/db", "password");
    EXPECT_TRUE(deleted) << "Engine should successfully delete the secret";
    EXPECT_FALSE(engine->get("/prod/db", "password").has_value()) << "Secret should be empty after deletion";
}

// ============================================================================
// 2. Test Fallback (Cache miss thì kéo từ RocksDB lên)
// ============================================================================
TEST_F(KallistoEngineTest, CacheMissFallback) {
    engine->change_sync_mode(KallistoEngine::SyncMode::IMMEDIATE);
    engine->put("/fallback", "key1", "data1");
    
    // Restart engine (hủy vùng nhớ Cache) to simulate memory loss
    engine.reset();
    engine = std::make_unique<KallistoEngine>(test_db_path);
    
    // Cache is now empty. The first get() should trigger the Fallback logic to RocksDB.
    auto entry = engine->get("/fallback", "key1");
    ASSERT_TRUE(entry.has_value()) << "Engine MUST retrieve from RocksDB on cache miss";
    EXPECT_EQ(entry->value, "data1");
}

// ============================================================================
// 3. Test Boundary: Khởi tạo biến TTL thiếu mặc định an toàn an toàn (3600)
// ============================================================================
TEST_F(KallistoEngineTest, DefaultTTLBoundary) {
    // Calling put without specifying TTL validates the header default 3600s assignment
    bool inserted = engine->put("/ttl_test", "key_no_ttl", "value_default");
    EXPECT_TRUE(inserted);
    
    auto retrieved = engine->get("/ttl_test", "key_no_ttl");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->value, "value_default") << "Engine must safely default the TTL instead of uninitialized memory garbage";
    EXPECT_EQ(retrieved->ttl, 3600) << "Explicitly verify that the TTL was set to 3600s default";
}

// ============================================================================
// 4. Test Concurrency: 10 threads gọi put() và change_sync_mode() liên tục
// ============================================================================
TEST_F(KallistoEngineTest, ConcurrencyStressTest) {
    constexpr int NUM_THREADS = 10;
    constexpr int OPS_PER_THREAD = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    // Stomping the atomic sync_mode_ and unsaved_ops_count_ simultaneously
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, i, &success_count, OPS_PER_THREAD]() {
            for (int j = 0; j < OPS_PER_THREAD; ++j) {
                std::string path = "/concurrent/" + std::to_string(i);
                std::string key = "key_" + std::to_string(j);
                std::string val = "val_" + std::to_string(i) + "_" + std::to_string(j);
                
                // Stress testing std::atomic<SyncMode> write
                if (j % 100 == 0) {
                    auto mode = (j % 2 == 0) ? KallistoEngine::SyncMode::BATCH : KallistoEngine::SyncMode::IMMEDIATE;
                    engine->change_sync_mode(mode);
                }

                if (engine->put(path, key, val)) {
                    success_count++;
                }

                // Stress testing std::atomic<SyncMode> read
                engine->get_sync_mode(); 
            }
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * OPS_PER_THREAD) 
        << "All concurrent puts must succeed without data races crashing the server";
        
    auto res = engine->get("/concurrent/5", "key_500");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, "val_5_500") 
        << "Data consistency must hold after chaotic multithreading";
}
