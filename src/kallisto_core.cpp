#include "kallisto/kallisto_core.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include <chrono>

namespace kallisto {

KallistoCore::KallistoCore(const std::string& db_path) {
    storage_ = std::make_unique<ShardedCuckooTable>(default_cuckoo_size);
    path_index_ = std::make_unique<TlsBTreeManager>(default_btree_degree, nullptr);
    rocksdb_persistence_ = std::make_unique<RocksDBStorage>(db_path);

    // Set initial sync mode to RocksDB
    rocksdb_persistence_->set_sync(sync_mode_.load(std::memory_order_relaxed) == SyncMode::IMMEDIATE);

    // Rebuild B-Tree index from WAL (RocksDB)
    rocksdb_persistence_->iterate_all([this](const SecretEntry& entry) {
        path_index_->insertPathIfAbsent(entry.path);
    });
}

KallistoCore::~KallistoCore() {
    // Make sure we save before dying if batching
    forceFlush();
}

std::string KallistoCore::buildFullKey(const std::string& path, const std::string& key) const {
    return path + ":" + key;
}

bool KallistoCore::put(const std::string& path, const std::string& key, const std::string& value, uint64_t ttl_secs) {
    std::string full_key = buildFullKey(path, key);

    SecretEntry entry;
    entry.key = key;
    entry.value = value;
    entry.path = path;
    entry.ttl = static_cast<uint32_t>(ttl_secs);
    entry.created_at = std::chrono::system_clock::now();

    // 1. Write to RocksDB (Sync is handled inside RocksDBStorage based on set_sync)
    bool disk_ok = rocksdb_persistence_->put(full_key, entry);
    if (!disk_ok) { 
		return false;
	}

    // 2. Batch Mode Counter (Lock-free stampede prevention)
    if (sync_mode_.load(std::memory_order_relaxed) == SyncMode::BATCH) {
        size_t ops = unsaved_ops_count_.fetch_add(1, std::memory_order_relaxed);
        if (ops >= sync_threshold) {
            size_t current = ops + 1;
            // Only the thread that successfully resets it to 0 gets to trigger the flush call
            if (unsaved_ops_count_.compare_exchange_strong(current, 0, std::memory_order_relaxed)) {
                forceFlush();
            }
        }
    }

    // 3. Write to RAM (Sharded Lock-Free)
    storage_->insert(full_key, entry);

    // 4. Update B-Tree logic
    path_index_->insertPathIfAbsent(path);

    return true;
}

std::optional<SecretEntry> KallistoCore::get(const std::string& path, const std::string& key) {
    std::string full_key = buildFullKey(path, key);

    // 1. Search in Cache (RAM)
    auto cached = storage_->lookup(full_key);
    if (cached.has_value()) {
        return cached; // Light-speed lookup
    }

    // 2. Cache Miss -> Read from RocksDB 
    auto disk = rocksdb_persistence_->get(full_key);
    
    // 3. Found on Disk -> Warm up the Cache
    if (disk.has_value()) {
        storage_->insert(full_key, disk.value());
        return disk;
    }

    return std::nullopt;
}

bool KallistoCore::del(const std::string& path, const std::string& key) {
    std::string full_key = buildFullKey(path, key);
    
    bool disk_ok = rocksdb_persistence_->del(full_key);
    if (!disk_ok) { 
		return false;
	}

    storage_->remove(full_key);
    return true; 
}

void KallistoCore::changeSyncMode(SyncMode mode) {
    sync_mode_.store(mode, std::memory_order_relaxed);
    if (rocksdb_persistence_) {
        rocksdb_persistence_->set_sync(mode == SyncMode::IMMEDIATE);
    }
}

KallistoCore::SyncMode KallistoCore::getSyncMode() const {
    return sync_mode_.load(std::memory_order_relaxed);
}

void KallistoCore::forceFlush() {
    if (rocksdb_persistence_) {
        rocksdb_persistence_->flush();
    }
}

void KallistoCore::checkAndSync() {
    forceFlush();
}

void KallistoCore::rebuildIndices(const std::vector<SecretEntry>& secrets) {
    for (const auto& s : secrets) {
        path_index_->insertPathIfAbsent(s.path);
    }
}

} // namespace kallisto
