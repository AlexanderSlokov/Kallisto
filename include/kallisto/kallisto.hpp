#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include "kallisto/secret_entry.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"  // Sharded for thread-safety (Phase 1.2)
#include "kallisto/tls_btree_manager.hpp"
#include "kallisto/storage_engine.hpp"

namespace kallisto {

class RocksDBStorage;  // Forward declaration

class KallistoServer {
public:
    KallistoServer();
    ~KallistoServer();

    /**
     * Stores a secret at a specific path.
     * 1. Validates path in B-Tree.
     * 2. Persists to RocksDB FIRST (Write-Ahead).
     * 3. Inserts into CuckooTable cache.
     */
    bool putSecret(const std::string& path, const std::string& key, const std::string& value);

    /**
     * Retrieves a secret.
     * 1. Validates path in B-Tree.
     * 2. O(1) Lookup in CuckooTable (hot cache).
     * 3. Cache miss → fallback to RocksDB → populate CuckooTable.
     */
    std::string getSecret(const std::string& path, const std::string& key);

    /**
     * Deletes a secret.
     * 1. Deletes from RocksDB FIRST.
     * 2. Removes from CuckooTable cache.
     */
    bool deleteSecret(const std::string& path, const std::string& key);

    enum class SyncMode {
        IMMEDIATE, // Sync to disk after every write (Safe, Slow)
        BATCH      // Sync only when threshold reached (Unsafe, Fast)
    };

    void setSyncMode(SyncMode mode);

    /**
     * Manually triggers a disk flush.
     */
    void force_save();

private:
    // Thread-safe sharded storage (64 partitions) — HOT CACHE
    std::unique_ptr<ShardedCuckooTable> storage;
    std::unique_ptr<TlsBTreeManager> path_index;
    std::unique_ptr<StorageEngine> persistence;  // Legacy snapshot (kept for migration)
    std::unique_ptr<RocksDBStorage> rocksdb_persistence;  // RocksDB persistence layer

    // Persistence Strategy
    SyncMode sync_mode = SyncMode::IMMEDIATE; // Default to safe
    size_t unsaved_ops_count = 0;
    const size_t SYNC_THRESHOLD = 10000; // Sync after 10,000 operations
    void check_and_sync();

    std::string buildFullKey(const std::string& path, const std::string& key) const;

    // Internal helper to rebuild B-Tree after loading from disk
    void rebuildIndices(const std::vector<SecretEntry>& secrets);
};

} // namespace kallisto
