#include "kallisto/kallisto_core.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {

KallistoCore::KallistoCore(const std::string& db_path) {
    auto kv = std::make_shared<engine::KvEngine>(db_path);
    default_kv_engine_ = kv.get();
    registry_.mount("secret", std::move(kv));
    LOG_INFO("[CORE] KallistoCore initialized with default KvEngine at 'secret'");
}

KallistoCore::~KallistoCore() {
    registry_.flushAll();
}

bool KallistoCore::put(const std::string& path, const std::string& key,
                       const std::string& value, uint64_t ttl_secs) {
    engine::SecretPayload payload{value, ttl_secs};
    auto res = default_kv_engine_->put_version(path + "/" + key, payload);
    return res.has_value();
}

std::optional<SecretEntry> KallistoCore::get(const std::string& path,
                                              const std::string& key) {
    auto res = default_kv_engine_->read_version(path + "/" + key, 0);
    if (!res) { 
		return std::nullopt;
	}
    
    SecretEntry entry;
    entry.path = path;
    entry.key = key;
    entry.value = res->value;
    entry.ttl = res->ttl;
    return entry;
}

bool KallistoCore::del(const std::string& path, const std::string& key) {
    auto meta = default_kv_engine_->read_metadata(path + "/" + key);
    if (!meta) { 
		return false;
	}

    auto res = default_kv_engine_->destroy_version(path + "/" + key, meta->current_version);
    
	return res.has_value();
}

void KallistoCore::changeSyncMode(SyncMode mode) {
    auto engine_mode = (mode == SyncMode::IMMEDIATE)
        ? engine::ISecretEngine::SyncMode::IMMEDIATE
        : engine::ISecretEngine::SyncMode::BATCH;
    default_kv_engine_->changeSyncMode(engine_mode);
}

KallistoCore::SyncMode KallistoCore::getSyncMode() const {
    auto mode = default_kv_engine_->getSyncMode();
    return (mode == engine::ISecretEngine::SyncMode::IMMEDIATE)
        ? SyncMode::IMMEDIATE : SyncMode::BATCH;
}

void KallistoCore::forceFlush() {
    registry_.flushAll();
}

} // namespace kallisto
