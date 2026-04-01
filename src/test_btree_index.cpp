#include <gtest/gtest.h>

#include "kallisto/btree_index.hpp"
#include "kallisto/tls_btree_manager.hpp"

#include <string>
#include <thread>

// =============================================================================
// BTREE INDEX TEST SUITE
//
// Problem Description:
//   BTreeIndex is the path validator for Kallisto. Before a secret lookup
//   hits the CuckooTable, the path must exist in the B-Tree. Corruption
//   here causes silent lookup failures or allows unauthorized path access.
//
// Coverage:
//   1. Basic insertion and validation
//   2. Duplicate insertion idempotency
//   3. High-volume splitting (100+ paths force root splits)
//   4. Boundary values (empty string, very long paths)
//   5. Deep copy correctness (RCU depends on this)
// =============================================================================

class BTreeIndexTest : public ::testing::Test {
protected:
    kallisto::BTreeIndex btree{3}; // min_degree = 3
};

TEST_F(BTreeIndexTest, BasicInsertionAndValidation) {
    EXPECT_TRUE(btree.insertPath("/api/v1/users"));
    EXPECT_TRUE(btree.insertPath("/api/v1/auth"));

    EXPECT_TRUE(btree.validatePath("/api/v1/users"));
    EXPECT_TRUE(btree.validatePath("/api/v1/auth"));
    EXPECT_FALSE(btree.validatePath("/api/v1/settings"));
}

TEST_F(BTreeIndexTest, DuplicateInsertionIsIdempotent) {
    EXPECT_TRUE(btree.insertPath("/api/v1/duplicate"));
    EXPECT_TRUE(btree.insertPath("/api/v1/duplicate"));
    EXPECT_TRUE(btree.validatePath("/api/v1/duplicate"));
}

TEST_F(BTreeIndexTest, HighVolumeSplitting) {
    for (int i = 0; i < 100; ++i) {
        btree.insertPath("/path/" + std::to_string(i));
    }

    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(btree.validatePath("/path/" + std::to_string(i)));
    }

    EXPECT_FALSE(btree.validatePath("/path/100"));
}

TEST_F(BTreeIndexTest, EmptyStringPath) {
    EXPECT_TRUE(btree.insertPath(""));
    EXPECT_TRUE(btree.validatePath(""));
}

TEST_F(BTreeIndexTest, VeryLongPath) {
    std::string long_path(10000, '/');
    EXPECT_TRUE(btree.insertPath(long_path));
    EXPECT_TRUE(btree.validatePath(long_path));
}

TEST_F(BTreeIndexTest, ValidateOnEmptyTreeReturnsFalse) {
    EXPECT_FALSE(btree.validatePath("/anything"));
}

TEST_F(BTreeIndexTest, DeepCopyPreservesData) {
    btree.insertPath("/original/path");

    kallisto::BTreeIndex clone(btree);
    EXPECT_TRUE(clone.validatePath("/original/path"));

    // Mutating clone should NOT affect original
    clone.insertPath("/clone/only");
    EXPECT_TRUE(clone.validatePath("/clone/only"));
    EXPECT_FALSE(btree.validatePath("/clone/only"));
}

TEST_F(BTreeIndexTest, DeepCopyDoesNotShareNodes) {
    btree.insertPath("/shared/check");
    kallisto::BTreeIndex clone(btree);

    // Insert into original — clone must remain unchanged
    btree.insertPath("/original/only");
    EXPECT_FALSE(clone.validatePath("/original/only"));
}

// =============================================================================
// TLS BTREE MANAGER TEST SUITE (RCU Concurrency)
//
// Problem Description:
//   TlsBTreeManager implements Envoy-style RCU for lock-free reads.
//   Each worker thread holds a thread-local snapshot. Writes create a
//   new master copy and dispatch it to all workers. Old copies go to
//   a GC queue for deferred deallocation.
//
// Coverage:
//   1. Initial snapshot is valid
//   2. insertPathIfAbsent creates new snapshot, rejects duplicates
//   3. Old snapshots remain immutable (RCU guarantee)
//   4. GC drains old snapshots without crashing
//   5. Thread safety under concurrent read/write
//   6. Boundary: multiple rapid updates
// =============================================================================

class TlsBTreeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // nullptr for workers triggers synchronous fallback (no event loop needed)
        manager_ = std::make_unique<kallisto::TlsBTreeManager>(3, nullptr);
    }

    void TearDown() override {
        manager_->drainGarbage();
    }

    std::unique_ptr<kallisto::TlsBTreeManager> manager_;
};

TEST_F(TlsBTreeManagerTest, InitialSnapshotIsValid) {
    auto snapshot = manager_->getLocalSnapshot();
    EXPECT_NE(snapshot, nullptr);
    EXPECT_FALSE(snapshot->validatePath("/missing/path"));
}

TEST_F(TlsBTreeManagerTest, InsertPathCreatesNewSnapshot) {
    auto old_snapshot = manager_->getLocalSnapshot();

    EXPECT_TRUE(manager_->insertPathIfAbsent("/new/path"));

    auto new_snapshot = manager_->getLocalSnapshot();
    EXPECT_TRUE(new_snapshot->validatePath("/new/path"));

    // Old snapshot must NOT contain the new path (RCU immutability)
    EXPECT_FALSE(old_snapshot->validatePath("/new/path"));

    // Pointers must differ (deep copy, not mutation)
    EXPECT_NE(old_snapshot.get(), new_snapshot.get());
}

TEST_F(TlsBTreeManagerTest, DuplicateInsertReturnsFalse) {
    EXPECT_TRUE(manager_->insertPathIfAbsent("/dup/path"));
    EXPECT_FALSE(manager_->insertPathIfAbsent("/dup/path"));
}

TEST_F(TlsBTreeManagerTest, GarbageCollectionDrainsWithoutCrash) {
    manager_->insertPathIfAbsent("/gc/path/1");
    manager_->insertPathIfAbsent("/gc/path/2");
    manager_->insertPathIfAbsent("/gc/path/3");

    EXPECT_NO_THROW(manager_->drainGarbage());

    // Data should still be accessible after GC
    auto snapshot = manager_->getLocalSnapshot();
    EXPECT_TRUE(snapshot->validatePath("/gc/path/1"));
    EXPECT_TRUE(snapshot->validatePath("/gc/path/3"));
}

TEST_F(TlsBTreeManagerTest, MultipleRapidUpdates) {
    for (int i = 0; i < 500; ++i) {
        manager_->insertPathIfAbsent("/rapid/" + std::to_string(i));
    }

    auto snapshot = manager_->getLocalSnapshot();
    for (int i = 0; i < 500; ++i) {
        EXPECT_TRUE(snapshot->validatePath("/rapid/" + std::to_string(i)));
    }
}

TEST_F(TlsBTreeManagerTest, ConcurrentReadersAndWriter) {
    // Problem: One writer thread inserts paths while reader threads
    // simultaneously fetch snapshots. Must not crash, deadlock, or
    // corrupt the B-Tree state.
    constexpr int num_iterations = 1000;

    auto writer_thread = std::thread([this]() {
        for (int i = 0; i < num_iterations; ++i) {
            manager_->insertPathIfAbsent("/thread/safe/" + std::to_string(i));
        }
    });

    auto reader_thread = std::thread([this]() {
        for (int i = 0; i < num_iterations; ++i) {
            auto snapshot = manager_->getLocalSnapshot();
            snapshot->validatePath("/thread/safe/" + std::to_string(i));
        }
    });

    writer_thread.join();
    reader_thread.join();

    // Force master sync to this thread's TLS
    manager_->insertPathIfAbsent("/dummy/force/sync");
    auto final_snapshot = manager_->getLocalSnapshot();

    for (int i = 0; i < num_iterations; ++i) {
        EXPECT_TRUE(final_snapshot->validatePath("/thread/safe/" + std::to_string(i)));
    }
}
