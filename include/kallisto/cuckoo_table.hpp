#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include "kallisto/secret_entry.hpp"
#include "kallisto/siphash.hpp"

namespace kallisto {

class CuckooTable {
public:
    /**
     * @param size The capacity of each of the two tables (number of buckets).
     * @param initial_capacity The initial capacity to reserve for the secret storage pool.
     */
    CuckooTable(size_t size = 1024, size_t initial_capacity = 1024);
    
    /**
     * Inserts a secret entry into the cuckoo table.
     * Uses the "kicking" mechanism to resolve collisions.
     * @return true if insertion was successful, false if a cycle was detected (full table).
     */
    bool insert(const std::string& key, const SecretEntry& entry);
    
    /**
     * Looks up an entry by key. O(1) worst-case.
     * @return The entry if found, std::nullopt otherwise.
     */
    std::optional<SecretEntry> lookup(const std::string& key) const;

    /**
     * Retrieves all entries from the table (for snapshotting).
     */
    std::vector<SecretEntry> get_all_entries() const;

    /**
     * Removes an entry by key.
     * @return true if entry was removed, false if not found.
     */
    struct MemoryStats {
        size_t bucket_count;
        size_t storage_capacity;
        size_t storage_used;
        size_t free_list_size;
        
        size_t bucket_memory_bytes;
        size_t storage_memory_bytes;
        size_t total_memory_allocated; // Approximate bytes
    };

    /**
     * Removes an entry by key.
     * @return true if entry was removed, false if not found.
     */
    bool remove(const std::string& key);

    MemoryStats get_memory_stats() const;

private:
    struct alignas(64) Bucket {
        struct Slot {
            uint32_t tag;   // Fingerprint (High bits of hash)
            uint32_t index; // Index into storage vector (0xFFFFFFFF = empty)
        } slots[8];
    };

    // Constants
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;
    static constexpr int BUCKETS_PER_CACHE_LINE = 1; // 64 bytes / 64 bytes
    static constexpr int SLOTS_PER_BUCKET = 8;

    std::vector<Bucket> table_1;
    std::vector<Bucket> table_2;

    // Storage Pool (Arena)
    // Concept: Instead of scattering SecretEntry objects in heap (via pointers),
    // we store them contiguously in a vector. Buckets hold 32-bit indices to this vector.
    std::vector<SecretEntry> storage;
    
    // Memory Management
    std::vector<uint32_t> free_list; // Stack (LIFO) for recycled indices
    uint32_t next_free_index = 0;    // High-water mark for new allocations

    // Concurrency & Stats
    mutable std::mutex write_mutex_; // Protects writers (insert/remove) interaction
    
    // Atomic Shadow Stats (Envoy-style non-blocking reads)
    std::atomic<size_t> shadow_storage_capacity_{0};
    std::atomic<size_t> shadow_storage_size_{0};
    std::atomic<size_t> shadow_free_list_size_{0};

    size_t capacity; // Number of buckets per table
    const int max_displacements = 500; // Increased due to higher load factor capability

    // Hash helpers return full 64-bit for Tag extraction
    uint64_t hash_1_full(const std::string& key) const;
    uint64_t hash_2_full(const std::string& key) const;
    
    // Tag generation: Extract high 32-bits from hash
    static inline uint32_t get_tag(uint64_t method) {
        uint32_t tag = static_cast<uint32_t>(method >> 32);
        return tag == 0 ? 1 : tag; // Tag 0 reserved? No, but let's just use raw bits.
    }
    
    void rehash();
};

} // namespace kallisto
