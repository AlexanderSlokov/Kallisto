#include "kallisto/kallisto.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include "kallisto/logger.hpp"
#include <unordered_set>

namespace kallisto {

KallistoServer::KallistoServer() {
    // 2^20, bitwise AND for fast modulo
    constexpr size_t cuckoo_table_capacity=1048576;
    storage = std::make_unique<ShardedCuckooTable>(cuckoo_table_capacity);

    // B-Tree with high degree (100) limit tree height to ~3 levels for 1M entries. 
    // This optimizes Cache Locality, reduces memory access times and CPU Pointer Chasing when validating paths.
    constexpr size_t btree_degree = 100;
    path_index = std::make_unique<TlsBTreeManager>(btree_degree, nullptr);
    
    // Starts EMPTY and warms up via cache-miss fallback to prevent self-DDoS RocksDB
    rocksdb_persistence = std::make_unique<RocksDBStorage>("/data/kallisto/rocksdb");
    if (!rocksdb_persistence->is_open()) {
        LOG_WARN("[CLI] RocksDB unavailable — running in-memory only!");
        rocksdb_persistence.reset();
    }
    
    // Rebuild B-Tree from RocksDB so cache-miss GETs aren't blocked
    if (rocksdb_persistence) {
        size_t count = 0;
        std::unordered_set<std::string> seen_paths;
        rocksdb_persistence->iterate_all([&](const SecretEntry& entry) {
            if (seen_paths.insert(entry.path).second) {
                path_index->update(entry.path);
            }
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

void KallistoServer::setSyncMode(SyncMode mode) {
    sync_mode = mode;
    if (sync_mode == SyncMode::IMMEDIATE) {
        LOG_INFO("[CONFIG] Switched to IMMEDIATE Sync Mode (Safe).");
        if (rocksdb_persistence) {
          rocksdb_persistence->set_sync(true);
        }
        force_save();
    } else {
        LOG_INFO("[CONFIG] Switched to BATCH Sync Mode (Performance).");
        if (rocksdb_persistence) {
          rocksdb_persistence->set_sync(false);
        }
    }
}

std::string KallistoServer::buildFullKey(const std::string& path, const std::string& key) const {
    return path + "/" + key;
}

bool KallistoServer::putSecret(const std::string& path, const std::string& key, const std::string& value) {
	
    path_index->update(path);
    
    SecretEntry entry;
    entry.key = key;
    entry.value = value;
    entry.path = path;
    entry.created_at = std::chrono::system_clock::now();
    entry.ttl = 3600;

    // Secret must be persisted on RocksDB first (Write-Ahead).
    // This ensures node crashes won't result in data loss.
    std::string full_key = buildFullKey(path, key);
    if (rocksdb_persistence) {
        bool persisted = rocksdb_persistence->put(full_key, entry);
        if (!persisted) {
            LOG_ERROR("[CLI] Failed to persist PUT operation to RocksDB for key: " + full_key);
            return false;
        }
    }

    bool result = storage->insert(full_key, entry);
    if (result) {
        unsaved_ops_count++;
        check_and_sync();    // Should check to flush to RocksDB if needed
    }
    return result;
}

std::string KallistoServer::getSecret(const std::string& path, const std::string& key) {
    LOG_DEBUG("[B-TREE] Validating path...");
    if (!path_index->get_local()->validatePath(path)) {
        LOG_ERROR("[B-TREE] Path validation failed: " + path);
        return "";
    }

    LOG_DEBUG("[CUCKOO] Looking up secret...");
    std::string full_key = buildFullKey(path, key);
    auto entry = storage->lookup(full_key);
    
    if (!entry.has_value() && rocksdb_persistence) {
        auto disk_entry = rocksdb_persistence->get(full_key);
        if (disk_entry.has_value()) {
            LOG_INFO("[ROCKSDB] Cache miss but found on disk. Populating CuckooTable.");
            storage->insert(full_key, disk_entry.value());
            path_index->update(disk_entry->path);
            return disk_entry->value;
        }
    }
    
    if (entry) {
        LOG_INFO("[CUCKOO] Value retrieved.");
        return entry->value;
    } else {
        LOG_WARN("[CUCKOO] Secret not found.");
        return "";
    }
}

bool KallistoServer::deleteSecret(const std::string& path, const std::string& key) {
    const std::string full_key = buildFullKey(path, key);
    
    // Delete at Persistence layer first to ensure it won't be "resurrected" after crash.
    if (rocksdb_persistence) {
        if (!rocksdb_persistence->del(full_key)) {
            LOG_ERROR("[CLI] Fail while persisting deletion for key: " + full_key + 
                      " at path: " + path + ". Deletion aborted to maintain consistency.");
            return false;
        }
    }
    
    // Delete at Cache
    bool cache_removed = storage->remove(full_key);
    
    // Use DEBUG level to avoid spam
    if (cache_removed) {
        LOG_DEBUG("[STORAGE] Successfully removed secret: " + full_key);
    } else {
        // Key may have been deleted from disk but not found in cache.
        // This is not necessarily an error, but it should be noted.
        LOG_WARN("[STORAGE] Key deleted from disk but not found in cache: " + full_key);
    }
    // If successfully deleted from disk, consider it successful (in terms of data persistence)
    return true; 
}

void KallistoServer::rebuildIndices(const std::vector<SecretEntry>& secrets) {

    if (secrets.empty()) {
        return;
    }

    auto start_time = std::chrono::steady_clock::now();
    LOG_INFO("[RECOVERY] Starting state reconstruction for " + std::to_string(secrets.size()) + " entries...");

    std::unordered_set<std::string> seen_paths;
    size_t processed_count = 0;

    for (const auto& entry : secrets) {
        // Only update B-Tree if this path hasn't been seen during this recovery
        if (seen_paths.insert(entry.path).second) {
            path_index->update(entry.path);
        }
        
        storage->insert(buildFullKey(entry.path, entry.key), entry);

        // Show progress every 100k entries
        processed_count++;
        if (processed_count % 100000 == 0) {
            LOG_INFO("[RECOVERY] Progress: " + std::to_string(processed_count) + " entries processed...");
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    LOG_INFO("[RECOVERY] Completed in " + std::to_string(duration) + " ms (" + 
             std::to_string(secrets.size()) + " entries restored).");
}

void KallistoServer::check_and_sync() {
    if (sync_mode == SyncMode::IMMEDIATE && rocksdb_persistence) {
        rocksdb_persistence->flush();
    } 
    
    else if (sync_mode == SyncMode::BATCH && unsaved_ops_count >= SYNC_THRESHOLD) {
        LOG_INFO("[PERSISTENCE] Threshold reached, flushing " + std::to_string(unsaved_ops_count) + " ops to disk...");
        force_save();
    }
}

void KallistoServer::force_save() {
    if (rocksdb_persistence) {
        rocksdb_persistence->flush();
    }
    unsaved_ops_count = 0;
}

} // namespace kallisto
