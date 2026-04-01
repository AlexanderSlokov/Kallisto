#include <gtest/gtest.h>

#include "kallisto/cuckoo_table.hpp"

#include <string>
#include <thread>
#include <vector>

using namespace kallisto;

// =============================================================================
// CUCKOO TABLE TEST SUITE
//
// Problem Description:
//   CuckooTable is the hot-path data structure for Kallisto's secret lookups.
//   It uses 8-way Cuckoo Hashing with dual tables and a storage pool arena.
//   Failures here mean data corruption, lost secrets, or security breaches.
//
// Coverage Strategy:
//   1. Basic CRUD operations (insert, lookup, update, remove)
//   2. Branch coverage on all return paths (found/not-found, full table)
//   3. Boundary values (empty keys, huge keys, minimal table size)
//   4. Memory stats validation (atomic shadow counters)
//   5. Concurrency safety (multi-threaded readers + writers)
//   6. Fuzz-like stress testing with high load factors
//   7. Free list recycling after remove + re-insert
// =============================================================================

class CuckooTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        table_ = std::make_unique<CuckooTable>(1024);
    }

    SecretEntry makeEntry(const std::string& key, const std::string& value) {
        SecretEntry entry;
        entry.key = key;
        entry.value = value;
        entry.path = "/test";
        return entry;
    }

    std::unique_ptr<CuckooTable> table_;
};

// ---------------------------------------------------------------------------
// 1. Basic CRUD Operations
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, InsertAndLookupSingleEntry) {
    auto entry = makeEntry("db_password", "s3cret!");
    EXPECT_TRUE(table_->insert("db_password", entry));

    auto result = table_->lookup("db_password");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "s3cret!");
    EXPECT_EQ(result->path, "/test");
}

TEST_F(CuckooTableTest, UpdateExistingKeyReplacesValue) {
    auto entry = makeEntry("api_key", "v1_old");
    table_->insert("api_key", entry);

    auto updated = makeEntry("api_key", "v2_new");
    EXPECT_TRUE(table_->insert("api_key", updated));

    auto result = table_->lookup("api_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "v2_new");
}

TEST_F(CuckooTableTest, RemoveExistingKeyReturnsTrue) {
    table_->insert("temp_key", makeEntry("temp_key", "to_delete"));
    EXPECT_TRUE(table_->remove("temp_key"));

    auto result = table_->lookup("temp_key");
    EXPECT_FALSE(result.has_value());
}

TEST_F(CuckooTableTest, RemoveNonExistentKeyReturnsFalse) {
    EXPECT_FALSE(table_->remove("ghost_key"));
}

TEST_F(CuckooTableTest, LookupNonExistentKeyReturnsNullopt) {
    auto result = table_->lookup("does_not_exist");
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// 2. getAllEntries (snapshot) Coverage
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, GetAllEntriesReturnsAllInsertedSecrets) {
    for (int i = 0; i < 50; ++i) {
        std::string key = "snapshot_key_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "val_" + std::to_string(i)));
    }

    auto all = table_->getAllEntries();
    EXPECT_EQ(all.size(), 50);
}

TEST_F(CuckooTableTest, GetAllEntriesOnEmptyTableReturnsEmpty) {
    auto all = table_->getAllEntries();
    EXPECT_TRUE(all.empty());
}

TEST_F(CuckooTableTest, GetAllEntriesExcludesRemovedKeys) {
    table_->insert("keep", makeEntry("keep", "yes"));
    table_->insert("drop", makeEntry("drop", "no"));
    table_->remove("drop");

    auto all = table_->getAllEntries();
    EXPECT_EQ(all.size(), 1);
    EXPECT_EQ(all[0].key, "keep");
}

// ---------------------------------------------------------------------------
// 3. Boundary Value Testing
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, EmptyStringKey) {
    auto entry = makeEntry("", "empty_key_value");
    EXPECT_TRUE(table_->insert("", entry));

    auto result = table_->lookup("");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "empty_key_value");
}

TEST_F(CuckooTableTest, VeryLongKey) {
    // 10KB key — tests that hashing and storage handle large allocations
    std::string long_key(10000, 'K');
    auto entry = makeEntry(long_key, "long_key_value");
    EXPECT_TRUE(table_->insert(long_key, entry));

    auto result = table_->lookup(long_key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "long_key_value");
}

TEST_F(CuckooTableTest, EmptyStringValue) {
    auto entry = makeEntry("null_value_key", "");
    EXPECT_TRUE(table_->insert("null_value_key", entry));

    auto result = table_->lookup("null_value_key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "");
}

TEST_F(CuckooTableTest, MinimalTableSize) {
    // Smallest possible table — tests edge-case bucket counts
    CuckooTable tiny_table(1, 1);

    for (int i = 0; i < 8; ++i) {
        std::string key = "tiny_" + std::to_string(i);
        tiny_table.insert(key, makeEntry(key, "v"));
    }

    auto result = tiny_table.lookup("tiny_0");
    ASSERT_TRUE(result.has_value());
}

// ---------------------------------------------------------------------------
// 4. Memory Stats (Atomic Shadow Counters)
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, MemoryStatsReflectInsertions) {
    auto stats_before = table_->getMemoryStats();

    for (int i = 0; i < 100; ++i) {
        std::string key = "mem_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "v"));
    }

    auto stats_after = table_->getMemoryStats();
    EXPECT_GT(stats_after.storage_used, stats_before.storage_used);
    EXPECT_GT(stats_after.total_memory_allocated, 0);
    EXPECT_GT(stats_after.bucket_count, 0);
    EXPECT_GT(stats_after.bucket_memory_bytes, 0);
}

TEST_F(CuckooTableTest, MemoryStatsReflectDeletions) {
    for (int i = 0; i < 50; ++i) {
        std::string key = "del_mem_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "v"));
    }

    for (int i = 0; i < 25; ++i) {
        table_->remove("del_mem_" + std::to_string(i));
    }

    auto stats = table_->getMemoryStats();
    // free_list_size should be > 0 after deletions (recycled slots)
    EXPECT_GT(stats.free_list_size, 0);
}

// ---------------------------------------------------------------------------
// 5. Free List Recycling (Remove then Re-insert)
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, RemovedSlotsAreRecycledOnReinsert) {
    // Insert, remove, then re-insert — storage arena should reuse freed indices
    for (int i = 0; i < 20; ++i) {
        std::string key = "recycle_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "original"));
    }

    auto stats_after_insert = table_->getMemoryStats();

    for (int i = 0; i < 20; ++i) {
        table_->remove("recycle_" + std::to_string(i));
    }

    // Re-insert same count — should reuse free list, not grow storage
    for (int i = 0; i < 20; ++i) {
        std::string key = "new_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "recycled"));
    }

    auto stats_after_recycle = table_->getMemoryStats();
    // storage_used should NOT have grown beyond the original high water mark
    EXPECT_LE(stats_after_recycle.storage_used, stats_after_insert.storage_used);
}

// ---------------------------------------------------------------------------
// 6. Fuzz-Like Stress Testing (High Load Factor)
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, HighLoadFactorStressTest) {
    // Problem: Insert a large number of keys to push the 8-way cuckoo hashing
    // toward its displacement limit. This tests the "kicking" mechanism.
    constexpr int num_entries = 5000;

    for (int i = 0; i < num_entries; ++i) {
        std::string key = "stress_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "sv_" + std::to_string(i)));
    }

    // Verify random samples survive
    for (int sample : {0, 999, 2500, 4999}) {
        std::string key = "stress_" + std::to_string(sample);
        auto result = table_->lookup(key);
        ASSERT_TRUE(result.has_value()) << "Key " << key << " should exist";
        EXPECT_EQ(result->value, "sv_" + std::to_string(sample));
    }
}

TEST_F(CuckooTableTest, UpdateUnderHighLoad) {
    // Problem: Updating an existing key in a crowded table should still work
    // and not corrupt neighbors.
    constexpr int num_entries = 1000;

    for (int i = 0; i < num_entries; ++i) {
        std::string key = "upd_load_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "old"));
    }

    // Update every 10th key
    for (int i = 0; i < num_entries; i += 10) {
        std::string key = "upd_load_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "updated"));
    }

    // Verify updated keys
    auto r0 = table_->lookup("upd_load_0");
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->value, "updated");

    // Verify non-updated neighbors are untouched
    auto r1 = table_->lookup("upd_load_1");
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->value, "old");
}

// ---------------------------------------------------------------------------
// 7. Concurrency Safety
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, ConcurrentReadersAndWriters) {
    // Problem Description: Simulate concurrent access where multiple threads
    // write to distinct key ranges while other threads read. The R/W lock
    // must prevent data races without deadlocking.
    constexpr int num_writer_threads = 4;
    constexpr int writes_per_thread = 500;
    constexpr int num_reader_threads = 4;

    // Pre-seed some data for readers
    for (int i = 0; i < 100; ++i) {
        std::string key = "seed_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "seeded"));
    }

    std::atomic<int> write_success{0};
    std::atomic<int> read_success{0};
    std::vector<std::thread> threads;
    threads.reserve(num_writer_threads + num_reader_threads);

    // Writers: each thread writes to its own key space
    for (int t = 0; t < num_writer_threads; ++t) {
        threads.emplace_back([this, t, &write_success]() {
            for (int i = 0; i < writes_per_thread; ++i) {
                std::string key = "w" + std::to_string(t) + "_" + std::to_string(i);
                if (table_->insert(key, makeEntry(key, "val"))) {
                    write_success.fetch_add(1);
                }
            }
        });
    }

    // Readers: continuously lookup seeded keys
    for (int t = 0; t < num_reader_threads; ++t) {
        threads.emplace_back([this, &read_success]() {
            for (int i = 0; i < 200; ++i) {
                std::string key = "seed_" + std::to_string(i % 100);
                if (table_->lookup(key).has_value()) {
                    read_success.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(write_success.load(), num_writer_threads * writes_per_thread);
    EXPECT_EQ(read_success.load(), num_reader_threads * 200);
}

TEST_F(CuckooTableTest, ConcurrentRemoveAndLookup) {
    // Problem Description: One thread removes keys while another reads them.
    // No crashes, no undefined behavior; reads return either the entry or nullopt.
    constexpr int num_keys = 200;

    for (int i = 0; i < num_keys; ++i) {
        std::string key = "race_" + std::to_string(i);
        table_->insert(key, makeEntry(key, "v"));
    }

    std::thread remover([this]() {
        for (int i = 0; i < num_keys; ++i) {
            table_->remove("race_" + std::to_string(i));
        }
    });

    std::thread reader([this]() {
        for (int i = 0; i < num_keys; ++i) {
            // Result is either valid or nullopt — must not crash
            table_->lookup("race_" + std::to_string(i));
        }
    });

    remover.join();
    reader.join();

    // After removal, all keys should be gone
    for (int i = 0; i < num_keys; ++i) {
        EXPECT_FALSE(table_->lookup("race_" + std::to_string(i)).has_value());
    }
}

// ---------------------------------------------------------------------------
// 8. Double Remove & Double Insert Idempotency
// ---------------------------------------------------------------------------

TEST_F(CuckooTableTest, DoubleRemoveReturnsFalseOnSecondAttempt) {
    table_->insert("once", makeEntry("once", "v"));
    EXPECT_TRUE(table_->remove("once"));
    EXPECT_FALSE(table_->remove("once")) << "Second remove should return false";
}

TEST_F(CuckooTableTest, DoubleInsertUpdatesInPlace) {
    table_->insert("dup", makeEntry("dup", "first"));
    table_->insert("dup", makeEntry("dup", "second"));

    auto all = table_->getAllEntries();
    int count = 0;
    for (const auto& e : all) {
        if (e.key == "dup") {
            count++;
        }
    }
    EXPECT_EQ(count, 1) << "Duplicate insert must update, not create a second entry";
}
