#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include "kallisto/sharded_cuckoo_table.hpp"

// =========================================================================================
// SHARDED CUCKOO TABLE TESTS
// =========================================================================================

class ShardedCuckooTableTest : public ::testing::Test {
protected:
    kallisto::ShardedCuckooTable table{64 * 1024}; // Small table sufficient for testing
    kallisto::SecretEntry entry1{"key1", "val1", "/path", std::chrono::system_clock::now(), 3600};
};

TEST_F(ShardedCuckooTableTest, BasicCrud) {
    // Scenario 1: Basic Insert
    EXPECT_TRUE(table.insert("key1", entry1));

    // Scenario 2: Lookup
    auto result = table.lookup("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "val1");

    // Scenario 3: Update
    entry1.value = "val2";
    EXPECT_TRUE(table.insert("key1", entry1));
    result = table.lookup("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "val2");

    // Scenario 4: Delete
    EXPECT_TRUE(table.remove("key1"));
    EXPECT_FALSE(table.lookup("key1").has_value());
}

TEST_F(ShardedCuckooTableTest, HashDistribution) {
    // Validate that our SipHash mechanism properly spreads keys across all shards
    std::vector<int> shard_counts(kallisto::ShardedCuckooTable::num_shards, 0);

    for (int i = 0; i < 10000; ++i) {
        std::string key = "dist_key_" + std::to_string(i);
        size_t shard_id = table.getShardIndex(key);

        ASSERT_LT(shard_id, kallisto::ShardedCuckooTable::num_shards);
        shard_counts[shard_id]++;
    }

    // Since siphash is statistically uniform, NO shard should be empty 
    // out of 64 shards when hashing 10,000 distinct items.
    for (size_t i = 0; i < shard_counts.size(); ++i) {
        EXPECT_GT(shard_counts[i], 0) << "Shard " << i << " received zero keys, indicating a hash distribution failure.";
    }
}

TEST_F(ShardedCuckooTableTest, ParallelIsolation) {
    // Simulate parallel writes across completely distinct keys
    // ShardedCuckoo should have near-zero lock contention here unlike standard Cuckoo.
    
    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    threads.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, ops_per_thread, &success_count]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                kallisto::SecretEntry e;
                e.key = "thread_" + std::to_string(t) + "_key_" + std::to_string(i);
                e.value = "val";
                if(table.insert(e.key, e)) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * ops_per_thread);

    // Verify presence randomly
    auto res = table.lookup("thread_0_key_500");
    EXPECT_TRUE(res.has_value());
}

TEST_F(ShardedCuckooTableTest, GetAllEntriesAndStats) {
    // Problem Description: Verify that getAllEntries and getMemoryStats 
    // properly aggregate data from all 64 shards. Boundary check on empty table.
    
    auto stats_empty = table.getMemoryStats();
    EXPECT_GT(stats_empty.bucket_count, 0);
    EXPECT_EQ(stats_empty.storage_used, 0);
    
    auto entries_empty = table.getAllEntries();
    EXPECT_TRUE(entries_empty.empty());

    // Insert 100 elements
    for (int i = 0; i < 100; ++i) {
        kallisto::SecretEntry e;
        e.key = "key_" + std::to_string(i);
        e.value = "val_" + std::to_string(i);
        table.insert(e.key, e);
    }
    
    auto stats = table.getMemoryStats();
    EXPECT_EQ(stats.storage_used, 100);
    EXPECT_GT(stats.total_memory_allocated, 0);
    
    auto entries = table.getAllEntries();
    EXPECT_EQ(entries.size(), 100);
}

TEST_F(ShardedCuckooTableTest, BoundaryValues) {
    // Problem Description: Test boundary values like empty strings for key/value
    // to ensure hashing and lookup don't crash on edge cases.
    kallisto::SecretEntry empty_entry;
    empty_entry.key = "";
    empty_entry.value = "";
    
    EXPECT_TRUE(table.insert("", empty_entry));
    auto res = table.lookup("");
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->value, "");
    
    EXPECT_TRUE(table.remove(""));
    EXPECT_FALSE(table.lookup("").has_value());
}
