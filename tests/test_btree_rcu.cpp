#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include "kallisto/btree_index.hpp"
#include "kallisto/tls_btree_manager.hpp"
#include "kallisto/event/worker.hpp"
#include "kallisto/event/dispatcher.hpp"

// =========================================================================================
// BTREE INDEX TESTS (Standalone)
// =========================================================================================

class BTreeIndexTest : public ::testing::Test {
protected:
    kallisto::BTreeIndex btree{3}; // t=3
};

TEST_F(BTreeIndexTest, BasicInsertionAndValidation) {
    // Scenario 1: Basic Insert
    EXPECT_TRUE(btree.insertPath("/api/v1/users"));
    EXPECT_TRUE(btree.insertPath("/api/v1/auth"));

    // Scenario 2: Validate existing
    EXPECT_TRUE(btree.validatePath("/api/v1/users"));
    EXPECT_TRUE(btree.validatePath("/api/v1/auth"));

    // Scenario 3: Validate missing
    EXPECT_FALSE(btree.validatePath("/api/v1/settings"));
}

TEST_F(BTreeIndexTest, DuplicateInsertion) {
    EXPECT_TRUE(btree.insertPath("/api/v1/duplicate"));
    // BTreeIndex::insertPath currently returns 'true' even if the path already exists
    // (it just returns early if search() is true). So we expect true.
    EXPECT_TRUE(btree.insertPath("/api/v1/duplicate")) << "Should return true (no-op) on duplicate insert per current implementation";
    EXPECT_TRUE(btree.validatePath("/api/v1/duplicate"));
}

TEST_F(BTreeIndexTest, HighVolumeSplitting) {
    // With degree t=3, nodes split quickly. 
    // Inserting 100 sequential paths forces multiple root splits.
    for (int i = 0; i < 100; ++i) {
        btree.insertPath("/path/" + std::to_string(i));
    }

    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(btree.validatePath("/path/" + std::to_string(i)));
    }
    
    EXPECT_FALSE(btree.validatePath("/path/100"));
}

// =========================================================================================
// TLS BTREE MANAGER TESTS (Concurrency & RCU)
// =========================================================================================

#include "kallisto/event/worker.hpp"

class MockWorkerPool : public kallisto::event::WorkerPool {
public:
    MockWorkerPool(size_t size) : size_(size) {}
    
    void start(std::function<void()> on_all_ready) override { if (on_all_ready) on_all_ready(); }
    void stop() override {}
    size_t size() const override { return size_; }
    kallisto::event::Worker& getWorker(size_t index) override { 
        static kallisto::event::Worker* dummy = nullptr; 
        return *dummy; 
    }
    uint64_t totalRequestsProcessed() const override { return 0; }
    
private:
    size_t size_;
};

class TlsBTreeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // We use nullptr for workers to trigger the synchronous fallback in update()
        // since we cannot easily mock the dispatcher's post() mechanism here without a running event loop.
        manager = std::make_unique<kallisto::TlsBTreeManager>(3, nullptr);
    }

    void TearDown() override {
        manager->drain_garbage();
    }

    std::unique_ptr<kallisto::TlsBTreeManager> manager;
};

TEST_F(TlsBTreeManagerTest, InitialSnapshot) {
    auto local_btree = manager->get_local();
    EXPECT_NE(local_btree, nullptr);
    EXPECT_FALSE(local_btree->validatePath("/missing/path"));
}

TEST_F(TlsBTreeManagerTest, UpdateCreatesNewSnapshot) {
    auto old_snapshot = manager->get_local();
    
    EXPECT_TRUE(manager->update("/new/path"));

    EXPECT_FALSE(old_snapshot->validatePath("/new/path"));

    auto new_snapshot = manager->get_local();
    EXPECT_TRUE(new_snapshot->validatePath("/new/path"));
    
    EXPECT_NE(old_snapshot.get(), new_snapshot.get());
}

TEST_F(TlsBTreeManagerTest, GarbageCollection) {
    auto initial_snapshot = manager->get_local();

    manager->update("/gc/path");

    manager->drain_garbage();
    
    EXPECT_FALSE(initial_snapshot->validatePath("/gc/path")); 
}

TEST_F(TlsBTreeManagerTest, ThreadSafety) {
    constexpr int NUM_ITERATIONS = 1000;
    
    auto writer_thread = std::thread([this]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            manager->update("/thread/safe/" + std::to_string(i));
        }
    });

    auto reader_thread = std::thread([this]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            auto local = manager->get_local();
            // In a real scenario without an event loop pushing to tls_btree_,
            // secondary threads will constantly fallback to pulling the master_btree_ lock.
            // This is safe but not entirely lock-free as designed for Worker threads.
            local->validatePath("/thread/safe/" + std::to_string(i)); 
        }
    });

    writer_thread.join();
    reader_thread.join();

    // Verify all contents ultimately populated to the master
    // We must ensure the main thread gets the latest snapshot.
    // Calling get_local() directly here might fetch an older thread_local snapshot 
    // unless we explicitly force a master sync.
    // A trick is to fetch it from a fresh thread or force an update that does nothing.
    manager->update("/dummy/force/sync"); // Forces master sync to thread_local
    auto final_snapshot = manager->get_local();
    
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        EXPECT_TRUE(final_snapshot->validatePath("/thread/safe/" + std::to_string(i)));
    }
}
