#pragma once

#include "kallisto/secret_entry.hpp"
#include <optional>
#include <string>

namespace kallisto::engine {

/**
 * ISecretEngine — Port interface for all secret engines.
 *
 * MACM Decision: Virtual dispatch costs ~8ns (0.3% of total request latency).
 * Dominated by syscall (~1000ns) and HTTP parsing (~800ns).
 * Use `final` on concrete classes to allow compiler devirtualization.
 */
class ISecretEngine {
public:
    virtual ~ISecretEngine() = default;

    // Prevent copying
    ISecretEngine(const ISecretEngine&) = delete;
    ISecretEngine& operator=(const ISecretEngine&) = delete;

    virtual bool put(const SecretEntry& entry) = 0;

    virtual std::optional<SecretEntry> get(const std::string& path,
                                           const std::string& key) = 0;

    virtual bool del(const std::string& path, const std::string& key) = 0;

    /** @return Engine type identifier (e.g., "kv", "transit") */
    virtual std::string engineType() const = 0;

    // --- Operational Controls ---

    enum class SyncMode { IMMEDIATE, BATCH };

    virtual void changeSyncMode(SyncMode mode) = 0;
    virtual SyncMode getSyncMode() const = 0;
    virtual void forceFlush() = 0;

protected:
    ISecretEngine() = default;
};

} // namespace kallisto::engine
