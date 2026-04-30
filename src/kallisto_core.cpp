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
    SecretEntry entry;
    entry.key = key;
    entry.value = value;
    entry.path = path;
    entry.ttl = static_cast<uint32_t>(ttl_secs);
    entry.created_at = std::chrono::system_clock::now();
    return default_kv_engine_->put(entry);
}

std::optional<SecretEntry> KallistoCore::get(const std::string& path,
                                              const std::string& key) {
    return default_kv_engine_->get(path, key);
}

bool KallistoCore::del(const std::string& path, const std::string& key) {
    return default_kv_engine_->del(path, key);
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
