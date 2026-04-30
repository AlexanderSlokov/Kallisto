#include "kallisto/engine/kv_engine.hpp"
#include "kallisto/rocksdb_storage.hpp"

namespace kallisto::engine {

KvEngine::KvEngine(const std::string& db_path) {
    storage_ = std::make_unique<ShardedCuckooTable>(default_cuckoo_size);
    path_index_ = std::make_unique<TlsBTreeManager>(default_btree_degree, nullptr);
    rocksdb_persistence_ = std::make_unique<RocksDBStorage>(db_path);

    // Set initial sync mode to RocksDB
    rocksdb_persistence_->setSync(sync_mode_.load(std::memory_order_relaxed) == SyncMode::IMMEDIATE);

    // Rebuild B-Tree index from WAL (RocksDB)
    rocksdb_persistence_->iterateAll([this](const SecretEntry& entry) {
        path_index_->insertPathIfAbsent(entry.path);
    });
}

KvEngine::~KvEngine() {
    // Make sure we save before dying if batching
    forceFlush();
}

std::string KvEngine::buildFullKey(const std::string& path, const std::string& key) const {
    return path + ":" + key;
}

bool KvEngine::put(const SecretEntry& entry) {
    std::string full_key = buildFullKey(entry.path, entry.key);

    bool disk_ok = rocksdb_persistence_->put(full_key, entry);
    if (!disk_ok) { 
        return false;
    }

    handleBatchSync();

    storage_->insert(full_key, entry);
    path_index_->insertPathIfAbsent(entry.path);

    return true;
}

void KvEngine::handleBatchSync() {
    if (sync_mode_.load(std::memory_order_relaxed) == SyncMode::BATCH) {
        size_t ops = unsaved_ops_count_.fetch_add(1, std::memory_order_relaxed);
        if (ops >= sync_threshold) {
            size_t current = ops + 1;
            if (unsaved_ops_count_.compare_exchange_strong(current, 0, std::memory_order_relaxed)) {
                forceFlush();
            }
        }
    }
}

std::optional<SecretEntry> KvEngine::get(const std::string& path, const std::string& key) {
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

bool KvEngine::del(const std::string& path, const std::string& key) {
    std::string full_key = buildFullKey(path, key);
    
    bool disk_ok = rocksdb_persistence_->del(full_key);
    if (!disk_ok) { 
		return false;
	}

    storage_->remove(full_key);
    return true; 
}

void KvEngine::changeSyncMode(SyncMode mode) {
    sync_mode_.store(mode, std::memory_order_relaxed);
    if (rocksdb_persistence_) {
        rocksdb_persistence_->setSync(mode == SyncMode::IMMEDIATE);
    }
}

ISecretEngine::SyncMode KvEngine::getSyncMode() const {
    return sync_mode_.load(std::memory_order_relaxed);
}

void KvEngine::forceFlush() {
    if (rocksdb_persistence_) {
        rocksdb_persistence_->flush();
    }
}

void KvEngine::checkAndSync() {
    forceFlush();
}

} // namespace kallisto::engine
