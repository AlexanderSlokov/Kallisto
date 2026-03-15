#include <string>
#include <gtest/gtest.h>
#include "kallisto/siphash.hpp"
#include "kallisto/cuckoo_table.hpp"

// =========================================================================================
// SIPHASH TESTS
// =========================================================================================
TEST(SipHashTest, Consistency) {
    // Scenario: Consistency Check
    // Why? A hash function must be deterministic. Same input + same key = same output.
    // If this fails, the entire storage lookup mechanism breaks.
    std::string input = "test_key";
    uint64_t hash1 = kallisto::SipHash::hash(input, 123, 456);
    uint64_t hash2 = kallisto::SipHash::hash(input, 123, 456);
    EXPECT_EQ(hash1, hash2) << "SipHash must be deterministic";
}

TEST(SipHashTest, AvalancheEffect) {
    // Scenario: Avalanche Effect
    // Why? A good hash function should change significantly if a single bit changes.
    // This prevents "Hash Flooding" attacks where attackers predict collisions.
    std::string input = "test_key";
    std::string input_modified = "test_key_"; 
    uint64_t hash1 = kallisto::SipHash::hash(input, 123, 456);
    uint64_t hash3 = kallisto::SipHash::hash(input_modified, 123, 456);
    EXPECT_NE(hash1, hash3) << "SipHash should produce different hashes for slightly different inputs";
}

// =========================================================================================
// CUCKOO TABLE TESTS
// =========================================================================================
class CuckooTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        table = std::make_unique<kallisto::CuckooTable>(1024);
        
        entry1.key = "db_pass";
        entry1.value = "secret123";
        entry1.path = "/prod";
    }

    std::unique_ptr<kallisto::CuckooTable> table;
    kallisto::SecretEntry entry1;
};

TEST_F(CuckooTableTest, BasicInsertAndLookup) {
    // Scenario 1: Basic Insert & Lookup
    // Why? The most fundamental requirement. Can we get back what we put in?
    bool inserted = table->insert("key1", entry1);
    EXPECT_TRUE(inserted) << "Should successfully insert a new key";

    auto result = table->lookup("key1");
    ASSERT_TRUE(result.has_value()) << "Should find the inserted key";
    EXPECT_EQ(result->value, "secret123") << "Value retrieved must match inserted value";
}

TEST_F(CuckooTableTest, UpdateExistingKey) {
    // Scenario 2: Update Existing Key
    // Why? KV Stores must act like a map. Inserting same key again should update value,
    // not create duplicates or fail silently.
    table->insert("key1", entry1);
    
    entry1.value = "new_secret";
    table->insert("key1", entry1);
    
    auto result_updated = table->lookup("key1");
    ASSERT_TRUE(result_updated.has_value());
    EXPECT_EQ(result_updated->value, "new_secret") << "Subsequent insert should update the value";
}

TEST_F(CuckooTableTest, NonExistentKey) {
    // Scenario 3: Non-existent Key
    // Why? We must correctly handle misses without crashing or returning garbage.
    auto result_missing = table->lookup("ghost_key");
    EXPECT_FALSE(result_missing.has_value()) << "Lookup for non-existent key should return empty";
}

TEST_F(CuckooTableTest, Deletion) {
    // Scenario 4: Deletion
    // Why? Deletion in Cuckoo Hash is tricky (just marking slot empty).
    // We need to verify the slot is actually reusable or at least not findable.
    table->insert("key1", entry1);
    
    bool deleted = table->remove("key1");
    EXPECT_TRUE(deleted) << "Remove should return true for existing key";
    
    auto result_deleted = table->lookup("key1");
    EXPECT_FALSE(result_deleted.has_value()) << "Key should be gone after deletion";
}

TEST_F(CuckooTableTest, CollisionHandling) {
    // Scenario 5: Collision Handling (Simulated)
    // Why? Cuckoo Hash's main feature is evicting keys to alternate locations.
    // While hard to deterministically force a collision without knowing the internal seeds,
    // inserting multiple keys validates the probing logic doesn't crash.
    for(int i=0; i<100; ++i) {
        kallisto::SecretEntry e;
        e.value = std::to_string(i);
        table->insert("k_" + std::to_string(i), e);
    }
    
    // Verify one random key exists
    auto res_50 = table->lookup("k_50");
    ASSERT_TRUE(res_50.has_value());
    EXPECT_EQ(res_50->value, "50") << "Should retrieve keys from a populated table";
}
