#pragma once

#include "kallisto/engine/engine_registry.hpp"
#include "kallisto/engine/kv_engine.hpp"
#include "kallisto/secret_entry.hpp"
#include <string>
#include <optional>

namespace kallisto {

/**
 * KallistoCore — Backward-compatible facade.
 *
 * Delegates all operations to the EngineRegistry.
 * Default: mounts a single KvEngine at "secret".
 *
 * Consumers (HttpHandler, UdsAdminHandler, tests) continue using
 * this class unchanged. Migration is invisible to them.
 */
class KallistoCore {
public:
    explicit KallistoCore(const std::string& db_path = "/var/lib/kallisto/data");
    ~KallistoCore();

    KallistoCore(const KallistoCore&) = delete;
    KallistoCore& operator=(const KallistoCore&) = delete;

    // --- Original API (unchanged signatures) ---
    bool put(const std::string& path, const std::string& key,
             const std::string& value, uint64_t ttl_secs = 3600);
    std::optional<SecretEntry> get(const std::string& path, const std::string& key);
    bool del(const std::string& path, const std::string& key);

    enum class SyncMode { IMMEDIATE, BATCH };
    void changeSyncMode(SyncMode mode);
    SyncMode getSyncMode() const;
    void forceFlush();

    // --- New Hexagonal API ---
    engine::EngineRegistry& registry() { return registry_; }
    const engine::EngineRegistry& registry() const { return registry_; }

private:
    engine::EngineRegistry registry_;
    engine::KvEngine* default_kv_engine_ = nullptr; // Non-owning shortcut
};

} // namespace kallisto
