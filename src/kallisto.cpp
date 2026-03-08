#include "kallisto/kallisto.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {

KallistoServer::KallistoServer() {
    // Phase 1.2: Use ShardedCuckooTable (64 partitions) for thread-safe access
    // Capacity: 2M items across 64 shards (~31K per shard)
    storage = std::make_unique<ShardedCuckooTable>(2000000);
    
    path_index = std::make_unique<TlsBTreeManager>(5, nullptr);
    
    // RocksDB persistence (replaces old StorageEngine snapshot)
    // CuckooTable starts EMPTY — warms up via cache-miss fallback
    rocksdb_persistence = std::make_unique<RocksDBStorage>("/data/kallisto/rocksdb");
    if (!rocksdb_persistence->is_open()) {
        LOG_WARN("[CLI] RocksDB unavailable — running in-memory only");
        rocksdb_persistence.reset();
    }
    
    // Legacy snapshot recovery (backward compatibility)
    persistence = std::make_unique<StorageEngine>();
    auto secrets = persistence->load_snapshot();
    if (!secrets.empty()) {
        LOG_INFO("[CLI] Migrating " + std::to_string(secrets.size()) + " entries from snapshot to RocksDB...");
        rebuild_indices(secrets);
        // Migrate to RocksDB
        if (rocksdb_persistence) {
            for (const auto& entry : secrets) {
                std::string full_key = build_full_key(entry.path, entry.key);
                rocksdb_persistence->put(full_key, entry);
            }
            LOG_INFO("[CLI] Migration complete.");
        }
    } else if (rocksdb_persistence) {
        // Normal startup: Rebuild B-Tree from RocksDB so cache-miss GETs aren't blocked
        size_t count = 0;
        rocksdb_persistence->iterate_all([&](const SecretEntry& entry) {
            path_index->update(entry.path);
            count++;
        });
        LOG_INFO("[CLI] Rebuilt B-Tree index with " + std::to_string(count) + " paths from RocksDB.");
    }
}

KallistoServer::~KallistoServer() {
    // Flush RocksDB on exit
    if (rocksdb_persistence) {
        rocksdb_persistence->flush();
    }
}

void KallistoServer::set_sync_mode(SyncMode mode) {
    sync_mode = mode;
    if (sync_mode == SyncMode::IMMEDIATE) {
        LOG_INFO("[CONFIG] Switched to IMMEDIATE Sync Mode (Safe).");
        if (rocksdb_persistence) rocksdb_persistence->set_sync(true);
        force_save();
    } else {
        LOG_INFO("[CONFIG] Switched to BATCH Sync Mode (Performance).");
        if (rocksdb_persistence) rocksdb_persistence->set_sync(false);
    }
}

std::string KallistoServer::build_full_key(const std::string& path, const std::string& key) const {
    return path + "/" + key;
}

bool KallistoServer::put_secret(const std::string& path, const std::string& key, const std::string& value) {
    // 1. Ensure path exists in path index
    path_index->update(path);
    
    // 2. Prepare secret entry
    SecretEntry entry;
    entry.key = key;
    entry.value = value;
    entry.path = path;
    entry.created_at = std::chrono::system_clock::now();
    entry.ttl = 3600; // Default TTL

    // 3. RocksDB FIRST (Write-Ahead mindset)
    std::string full_key = build_full_key(path, key);
    if (rocksdb_persistence) {
        bool persisted = rocksdb_persistence->put(full_key, entry);
        if (!persisted) {
            LOG_ERROR("[CLI] Failed to persist to RocksDB");
            return false;
        }
    }

    // 4. Store in CuckooTable (cache)
    bool result = storage->insert(full_key, entry);
    
    return result;
}

std::string KallistoServer::get_secret(const std::string& path, const std::string& key) {
    // Step 1: Validate Path using B-Tree
    LOG_DEBUG("[B-TREE] Validating path...");
    if (!path_index->get_local()->validate_path(path)) {
        LOG_ERROR("[B-TREE] Path validation failed: " + path);
        return "";
    }

    // Step 2: Try CuckooTable (hot cache, sub-µs)
    LOG_DEBUG("[CUCKOO] Looking up secret...");
    std::string full_key = build_full_key(path, key);
    auto entry = storage->lookup(full_key);
    
    // Step 3: Cache miss → fallback to RocksDB
    if (!entry.has_value() && rocksdb_persistence) {
        auto disk_entry = rocksdb_persistence->get(full_key);
        if (disk_entry.has_value()) {
            LOG_INFO("[ROCKSDB] Cache miss, found on disk. Populating CuckooTable.");
            storage->insert(full_key, disk_entry.value());
            path_index->update(disk_entry->path);
            return disk_entry->value;
        }
    }
    
    if (entry) {
        LOG_INFO("[CUCKOO] HIT! Value retrieved.");
        return entry->value;
    } else {
        LOG_WARN("[CUCKOO] MISS! Secret not found.");
        return "";
    }
}

bool KallistoServer::delete_secret(const std::string& path, const std::string& key) {
    std::string full_key = build_full_key(path, key);
    
    // RocksDB FIRST
    if (rocksdb_persistence) {
        bool persisted = rocksdb_persistence->del(full_key);
        if (!persisted) {
            LOG_ERROR("[CLI] Failed to delete from RocksDB");
            return false;
        }
    }
    
    bool result = storage->remove(full_key);
    return result;
}

void KallistoServer::rebuild_indices(const std::vector<SecretEntry>& secrets) {
    LOG_INFO("[RECOVERY] Rebuilding state from " + std::to_string(secrets.size()) + " entries...");
    for (const auto& entry : secrets) {
        // 1. Rebuild B-Tree
        path_index->update(entry.path);
        
        // 2. Re-populate Cuckoo Table
        std::string full_key = build_full_key(entry.path, entry.key);
        storage->insert(full_key, entry);
    }
    LOG_INFO("[RECOVERY] Completed.");
}

void KallistoServer::check_and_sync() {
    // With RocksDB, every write is already persisted.
    // This is kept for legacy SyncMode::IMMEDIATE flush behavior.
    if (sync_mode == SyncMode::IMMEDIATE && rocksdb_persistence) {
        rocksdb_persistence->flush();
    }
}

void KallistoServer::force_save() {
    if (rocksdb_persistence) {
        rocksdb_persistence->flush();
    }
    unsaved_ops_count = 0;
}

} // namespace kallisto
