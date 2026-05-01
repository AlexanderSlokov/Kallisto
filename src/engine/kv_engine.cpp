#include "kallisto/engine/kv_engine.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include <chrono>
#include <cstring>

namespace kallisto::engine {

namespace {

// ==========================================
// Serialization Helpers
// ==========================================

std::string serializePayload(const SecretPayload& payload) {
    std::string buf;
    buf.reserve(8 + payload.value.size());
    uint64_t ttl = payload.ttl;
    buf.append(reinterpret_cast<const char*>(&ttl), sizeof(ttl));
    buf.append(payload.value);
    return buf;
}

std::optional<SecretPayload> deserializePayload(const std::string& data) {
    if (data.size() < 8) { 
		return std::nullopt;
	}
    SecretPayload p;
    std::memcpy(&p.ttl, data.data(), 8);
    p.value = data.substr(8);
    return p;
}

std::string serializeMetadata(const KeyMetadata& meta) {
    std::string buf;
    size_t size = 4 + 4 + 1 + 8 + 4 + meta.versions.size() * sizeof(VersionState);
    buf.reserve(size);
    buf.append(reinterpret_cast<const char*>(&meta.current_version), 4);
    buf.append(reinterpret_cast<const char*>(&meta.max_versions), 4);
    buf.append(reinterpret_cast<const char*>(&meta.cas_required), 1);
    buf.append(reinterpret_cast<const char*>(&meta.delete_version_after_ms), 8);
    uint32_t v_size = meta.versions.size();
    buf.append(reinterpret_cast<const char*>(&v_size), 4);
    if (v_size > 0) {
        buf.append(reinterpret_cast<const char*>(meta.versions.data()), v_size * sizeof(VersionState));
    }
    return buf;
}

std::optional<KeyMetadata> deserializeMetadata(const std::string& data) {
    if (data.size() < 21) { 
		return std::nullopt;
	}
    KeyMetadata m;
    const char* ptr = data.data();
    std::memcpy(&m.current_version, ptr, 4); ptr += 4;
    std::memcpy(&m.max_versions, ptr, 4); ptr += 4;
    std::memcpy(&m.cas_required, ptr, 1); ptr += 1;
    std::memcpy(&m.delete_version_after_ms, ptr, 8); ptr += 8;
    uint32_t v_size;
    std::memcpy(&v_size, ptr, 4); ptr += 4;
    if (v_size > 0 && data.size() >= 21 + v_size * sizeof(VersionState)) {
        m.versions.resize(v_size);
        std::memcpy(m.versions.data(), ptr, v_size * sizeof(VersionState));
    }
    return m;
}

std::string buildMetaKey(std::string_view path) {
    return "m:" + std::string(path);
}

std::string buildVersionKey(std::string_view path, uint32_t version) {
    return "v:" + std::string(path) + ":" + std::to_string(version);
}

uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ==========================================
// CuckooTable Adapter (Legacy Seam)
// ==========================================

void cacheRaw(ShardedCuckooTable* cache, const std::string& raw_key, const std::string& serialized) {
    SecretEntry entry;
    entry.path = raw_key;
    entry.key = "";
    entry.value = serialized;
    entry.ttl = 0;
    cache->insert(raw_key, entry);
}

std::optional<std::string> getCachedRaw(ShardedCuckooTable* cache, const std::string& raw_key) {
    auto res = cache->lookup(raw_key);
    if (res) {
		return res->value;
	}
    return std::nullopt;
}

void uncacheRaw(ShardedCuckooTable* cache, const std::string& raw_key) {
    cache->remove(raw_key);
}

std::optional<std::string> readRawOptimistic(RocksDBStorage* db, ShardedCuckooTable* cache, const std::string& key) {
    if (auto cached = getCachedRaw(cache, key)) {
        return cached;
    }
    if (auto disk = db->getRaw(key)) {
        cacheRaw(cache, key, *disk);
        return disk;
    }
    return std::nullopt;
}

} // namespace

// ==========================================
// Engine Implementation
// ==========================================

KvEngine::KvEngine(const std::string& db_path) {
    storage_ = std::make_unique<ShardedCuckooTable>(default_cuckoo_size);
    path_index_ = std::make_unique<TlsBTreeManager>(default_btree_degree, nullptr);
    rocksdb_persistence_ = std::make_unique<RocksDBStorage>(db_path);

    rocksdb_persistence_->setSync(sync_mode_.load(std::memory_order_relaxed) == SyncMode::IMMEDIATE);

    // Rebuild B-Tree index from WAL
    rocksdb_persistence_->iterateAll([this](const SecretEntry& entry) {
        path_index_->insertPathIfAbsent(entry.path);
    });

    async_worker_ = std::thread(&KvEngine::asyncWorkerLoop, this);
}

KvEngine::~KvEngine() {
    async_running_.store(false, std::memory_order_relaxed);
    if (async_worker_.joinable()) {
        async_worker_.join();
    }
    forceFlush();
}

void KvEngine::enqueueOrExecute(AsyncOp::Type type, const std::string& key, const std::string& value) {
    if (sync_mode_.load(std::memory_order_relaxed) == SyncMode::IMMEDIATE) {
        if (type == AsyncOp::Type::PUT) {
            rocksdb_persistence_->putRaw(key, value);
        } else {
            rocksdb_persistence_->delRaw(key);
        }
    } else {
        // Zero-blocking, lock-free enqueue (nanosecond scale)
        AsyncOp op{type, key, value};
        while (!async_queue_.enqueue(std::move(op))) {
            // If queue is full (extreme edge case), yield to consumer
            std::this_thread::yield();
        }
    }
}

void KvEngine::asyncWorkerLoop() {
    AsyncOp op;
    std::vector<RocksDBStorage::BatchOp> batch;
    batch.reserve(20000);

    while (async_running_.load(std::memory_order_relaxed)) {
        bool has_work = false;
        
        // Drain lock-free queue into batch
        while (async_queue_.dequeue(op)) {
            has_work = true;
            auto btype = (op.type == AsyncOp::Type::PUT) 
                       ? RocksDBStorage::BatchOp::Type::PUT 
                       : RocksDBStorage::BatchOp::Type::DEL;
            batch.push_back({btype, std::move(op.key), std::move(op.value)});
            
            // Limit batch size to prevent long RocksDB lock stalls
            if (batch.size() >= 20000) {
                break;
            }
        }
        
        if (has_work) {
            rocksdb_persistence_->applyBatch(batch);
            batch.clear();
        } else {
            // Sleep if idle to prevent 100% CPU burn
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Flush any remaining ops on shutdown
    while (async_queue_.dequeue(op)) {
        auto btype = (op.type == AsyncOp::Type::PUT) 
                   ? RocksDBStorage::BatchOp::Type::PUT 
                   : RocksDBStorage::BatchOp::Type::DEL;
        batch.push_back({btype, std::move(op.key), std::move(op.value)});
    }
    if (!batch.empty()) {
        rocksdb_persistence_->applyBatch(batch);
    }
}

std::string KvEngine::buildFullKey(const std::string& path, const std::string& key) const {
    return path + ":" + key; // V1 legacy fallback
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

tl::expected<KeyMetadata, EngineError> KvEngine::read_metadata(std::string_view path) {
    auto raw = readRawOptimistic(rocksdb_persistence_.get(), storage_.get(), buildMetaKey(path));
    if (!raw) { 
		return tl::unexpected(EngineError::NotFound);
	}
    
    auto meta = deserializeMetadata(*raw);
    if (!meta) { 
		return tl::unexpected(EngineError::StorageError);
	}
    
    return *meta;
}

tl::expected<SecretPayload, EngineError> KvEngine::read_version(std::string_view path, uint32_t version) {
    auto meta_res = read_metadata(path);
    if (!meta_res) { 
		return tl::unexpected(meta_res.error());
	}
    
    KeyMetadata meta = meta_res.value();
    uint32_t target_version = (version == 0) ? meta.current_version : version;
    
    if (target_version == 0 || target_version > meta.current_version) {
        return tl::unexpected(EngineError::InvalidVersion);
    }
    
    const VersionState* state = nullptr;
    for (const auto& vs : meta.versions) {
        if (vs.version_id == target_version) {
            state = &vs;
            break;
        }
    }
    
    if (!state) { 
		return tl::unexpected(EngineError::InvalidVersion);
	}
    if (state->destroyed) { 
		return tl::unexpected(EngineError::Destroyed);
	}
    if (state->deletion_time_ms > 0) { 
		return tl::unexpected(EngineError::SoftDeleted);
	}
    
    auto raw_payload = readRawOptimistic(rocksdb_persistence_.get(), storage_.get(), buildVersionKey(path, target_version));
    if (!raw_payload) { 
		return tl::unexpected(EngineError::StorageError); 
	}
    
    auto payload = deserializePayload(*raw_payload);
    if (!payload) { 
		return tl::unexpected(EngineError::StorageError);
	}
    
    return *payload;
}

tl::expected<void, EngineError> KvEngine::put_version(std::string_view path, const SecretPayload& payload, std::optional<uint32_t> cas) {
    std::string mkey = buildMetaKey(path);
    KeyMetadata meta;
    
    if (auto raw_meta = readRawOptimistic(rocksdb_persistence_.get(), storage_.get(), mkey)) {
        if (auto m = deserializeMetadata(*raw_meta)) { 
			meta = *m;
		}
    }
    
    if (cas.has_value() && meta.current_version != cas.value()) { 
		return tl::unexpected(EngineError::CasMismatch);
	}
    
    meta.current_version++;
    VersionState vs;
    vs.version_id = meta.current_version;
    vs.created_time_ms = nowMs();
    vs.deletion_time_ms = 0;
    vs.destroyed = false;
    meta.versions.push_back(vs);
    
    std::string vkey = buildVersionKey(path, vs.version_id);
    
    enqueueOrExecute(AsyncOp::Type::PUT, vkey, serializePayload(payload));
    cacheRaw(storage_.get(), vkey, serializePayload(payload));
    
    enqueueOrExecute(AsyncOp::Type::PUT, mkey, serializeMetadata(meta));
	cacheRaw(storage_.get(), mkey, serializeMetadata(meta));
    
    path_index_->insertPathIfAbsent(std::string(path));
	
	handleBatchSync();
    
    return {};
}

tl::expected<void, EngineError> KvEngine::soft_delete(std::string_view path, uint32_t version) {
    std::string mkey = buildMetaKey(path);
    auto raw_meta = readRawOptimistic(rocksdb_persistence_.get(), storage_.get(), mkey);
    if (!raw_meta) { 
		return tl::unexpected(EngineError::NotFound);
	}
    
    auto meta = deserializeMetadata(*raw_meta);
    if (!meta) { 
		return tl::unexpected(EngineError::StorageError);
	}
    
    bool found = false;
    for (auto& vs : meta->versions) {
        if (vs.version_id == version) {
            vs.deletion_time_ms = nowMs();
            found = true;
            break;
        }
    }
    if (!found) { 
		return tl::unexpected(EngineError::InvalidVersion);
	}
    
    enqueueOrExecute(AsyncOp::Type::PUT, mkey, serializeMetadata(*meta));
    cacheRaw(storage_.get(), mkey, serializeMetadata(*meta));
    handleBatchSync();
    
    return {};
}

tl::expected<void, EngineError> KvEngine::destroy_version(std::string_view path, uint32_t version) {
    std::string mkey = buildMetaKey(path);
    auto raw_meta = readRawOptimistic(rocksdb_persistence_.get(), storage_.get(), mkey);
    if (!raw_meta) { 
		return tl::unexpected(EngineError::NotFound);
	}
    
    auto meta = deserializeMetadata(*raw_meta);
    if (!meta) { 
		return tl::unexpected(EngineError::StorageError);
	}
    
    bool found = false;
    for (auto& vs : meta->versions) {
        if (vs.version_id == version) {
            vs.destroyed = true;
            found = true;
            break;
        }
    }
    if (!found) { 
		return tl::unexpected(EngineError::InvalidVersion);
	}
    
    std::string vkey = buildVersionKey(path, version);
    enqueueOrExecute(AsyncOp::Type::DEL, vkey, "");
    uncacheRaw(storage_.get(), vkey);
    
    enqueueOrExecute(AsyncOp::Type::PUT, mkey, serializeMetadata(*meta));
    cacheRaw(storage_.get(), mkey, serializeMetadata(*meta));
    handleBatchSync();
    
    return {};
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
