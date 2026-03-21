#include "kallisto/kallisto_engine.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include <chrono>

namespace kallisto {

KallistoEngine::KallistoEngine(const std::string& db_path) {
    storage_ = std::make_unique<ShardedCuckooTable>(2000000); // 2M slots
    path_index_ = std::make_unique<TlsBTreeManager>(100, nullptr); // BTree degree 100
    rocksdb_persistence_ = std::make_unique<RocksDBStorage>(db_path);

    // Set initial sync mode to RocksDB
    rocksdb_persistence_->set_sync(sync_mode_.load(std::memory_order_relaxed) == SyncMode::IMMEDIATE);

    // Rebuild B-Tree index from WAL (RocksDB)
    rocksdb_persistence_->iterate_all([this](const SecretEntry& entry) {
        path_index_->update(entry.path);
    });
}

KallistoEngine::~KallistoEngine() {
    // Make sure we save before dying if batching
    force_flush();
}

std::string KallistoEngine::build_full_key(const std::string& path, const std::string& key) const {
    return path + ":" + key;
}

bool KallistoEngine::put(const std::string& path, const std::string& key, const std::string& value, uint64_t ttl_secs) {
    std::string full_key = build_full_key(path, key);

    SecretEntry entry;
    entry.key = key;
    entry.value = value;
    entry.path = path;
    entry.ttl = static_cast<uint32_t>(ttl_secs);
    entry.created_at = std::chrono::system_clock::now();

    // 1. Write to RocksDB (Sync is handled inside RocksDBStorage based on set_sync)
    bool disk_ok = rocksdb_persistence_->put(full_key, entry);
    if (!disk_ok) return false;

    // 2. Batch Mode Counter (Lock-free stampede prevention)
    if (sync_mode_.load(std::memory_order_relaxed) == SyncMode::BATCH) {
        size_t ops = unsaved_ops_count_.fetch_add(1, std::memory_order_relaxed);
        if (ops >= SYNC_THRESHOLD) {
            size_t current = ops + 1;
            // Only the thread that successfully resets it to 0 gets to trigger the flush call
            if (unsaved_ops_count_.compare_exchange_strong(current, 0, std::memory_order_relaxed)) {
                force_flush();
            }
        }
    }

    // 3. Write to RAM (Sharded Lock-Free)
    storage_->insert(full_key, entry);

    // 4. Update B-Tree logic
    path_index_->update(path);

    return true;
}

std::optional<SecretEntry> KallistoEngine::get(const std::string& path, const std::string& key) {
    std::string full_key = build_full_key(path, key);

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

bool KallistoEngine::del(const std::string& path, const std::string& key) {
    std::string full_key = build_full_key(path, key);
    
    bool disk_ok = rocksdb_persistence_->del(full_key);
    if (!disk_ok) return false;

    storage_->remove(full_key);
    return true; 
}

void KallistoEngine::change_sync_mode(SyncMode mode) {
    sync_mode_.store(mode, std::memory_order_relaxed);
    if (rocksdb_persistence_) {
        rocksdb_persistence_->set_sync(mode == SyncMode::IMMEDIATE);
    }
}

KallistoEngine::SyncMode KallistoEngine::get_sync_mode() const {
    return sync_mode_.load(std::memory_order_relaxed);
}

void KallistoEngine::force_flush() {
    if (rocksdb_persistence_) {
        rocksdb_persistence_->flush();
    }
}

void KallistoEngine::check_and_sync() {
    force_flush();
}

void KallistoEngine::rebuild_indices(const std::vector<SecretEntry>& secrets) {
    for (const auto& s : secrets) {
        path_index_->update(s.path);
    }
}

} // namespace kallisto
