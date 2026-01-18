#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {

ShardedCuckooTable::ShardedCuckooTable(size_t total_capacity) {
    // Calculate per-shard parameters
    size_t items_per_shard = total_capacity / NUM_SHARDS;
    
    // CuckooTable architecture:
    // - 2 tables (table_1, table_2)
    // - 8 slots per bucket
    // - Total slots = buckets * 8 * 2 = 16 * buckets
    //
    // For 50% load factor: items = 0.5 * total_slots
    // items = 0.5 * 16 * buckets
    // buckets = items / 8
    //
    // (O5 Council decision: Gemini correction from /4 to /8)
    size_t buckets_per_shard = items_per_shard / 8;
    
    // Ensure minimum bucket count
    if (buckets_per_shard < 64) {
        buckets_per_shard = 64;
    }
    
    info("ShardedCuckooTable: Creating " + std::to_string(NUM_SHARDS) + 
         " shards, " + std::to_string(buckets_per_shard) + " buckets each, " +
         std::to_string(items_per_shard) + " items capacity per shard");
    
    for (auto& shard : shards_) {
        shard = std::make_unique<CuckooTable>(buckets_per_shard, items_per_shard);
    }
}

bool ShardedCuckooTable::insert(const std::string& key, const SecretEntry& entry) {
    return getShard(key)->insert(key, entry);
}

std::optional<SecretEntry> ShardedCuckooTable::lookup(const std::string& key) const {
    return getShard(key)->lookup(key);
}

bool ShardedCuckooTable::remove(const std::string& key) {
    return getShard(key)->remove(key);
}

CuckooTable::MemoryStats ShardedCuckooTable::get_memory_stats() const {
    CuckooTable::MemoryStats total{};
    
    for (const auto& shard : shards_) {
        auto stats = shard->get_memory_stats();
        total.bucket_count += stats.bucket_count;
        total.storage_capacity += stats.storage_capacity;
        total.storage_used += stats.storage_used;
        total.free_list_size += stats.free_list_size;
        total.bucket_memory_bytes += stats.bucket_memory_bytes;
        total.storage_memory_bytes += stats.storage_memory_bytes;
        total.total_memory_allocated += stats.total_memory_allocated;
    }
    
    return total;
}

std::vector<SecretEntry> ShardedCuckooTable::get_all_entries() const {
    std::vector<SecretEntry> all;
    
    // Pre-allocate based on expected size
    size_t estimated_total = 0;
    for (const auto& shard : shards_) {
        estimated_total += shard->get_memory_stats().storage_used;
    }
    all.reserve(estimated_total);
    
    for (const auto& shard : shards_) {
        auto entries = shard->get_all_entries();
        all.insert(all.end(), entries.begin(), entries.end());
    }
    
    return all;
}

} // namespace kallisto
