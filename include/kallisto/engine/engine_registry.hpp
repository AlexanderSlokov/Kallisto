#pragma once

#include "kallisto/engine/i_secret_engine.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace kallisto::engine {

/**
 * EngineRegistry — Routes requests to the correct engine by path prefix.
 *
 * Usage:
 *   registry.mount("secret", std::make_unique<KvEngine>(db_path));
 *   auto* engine = registry.resolve("secret");
 */
class EngineRegistry {
public:
    /**
     * Mount an engine at the given prefix.
     * @param prefix Engine mount point (e.g., "secret", "transit")
     * @param engine Ownership transferred to registry
     */
    void mount(const std::string& prefix, std::shared_ptr<ISecretEngine> engine);

    /**
     * Resolve a prefix to the mounted engine.
     * @return Pointer to engine, or nullptr if no engine mounted at prefix
     */
    ISecretEngine* resolve(const std::string& prefix) const;

    /** @return All mounted engine prefixes */
    std::vector<std::string> mountedPrefixes() const;

    /** Flush all engines (for graceful shutdown) */
    void flushAll();

private:
    std::unordered_map<std::string, std::shared_ptr<ISecretEngine>> engines_;
    mutable std::mutex mutex_; // Protects mount/unmount (rare, not hot path)
};

} // namespace kallisto::engine
