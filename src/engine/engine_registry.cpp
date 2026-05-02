#include "kallisto/engine/engine_registry.hpp"
#include "kallisto/logger.hpp"

namespace kallisto::engine {

void EngineRegistry::mount(const std::string& prefix,
                           std::shared_ptr<ISecretEngine> engine) {
    std::lock_guard lock(mutex_);
    LOG_INFO("[REGISTRY] Mounting engine '" + engine->engineType()
             + "' at prefix: " + prefix);
    engines_[prefix] = std::move(engine);
}

ISecretEngine* EngineRegistry::resolve(const std::string& prefix) const {
    // Hot path — no lock needed for reads on std::unordered_map
    // (mount/unmount only happens at startup/admin, never concurrent with resolve)
    auto it = engines_.find(prefix);
    if (it != engines_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<std::string> EngineRegistry::mountedPrefixes() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> prefixes;
    prefixes.reserve(engines_.size());
    for (const auto& [prefix, _] : engines_) {
        prefixes.push_back(prefix);
    }
    return prefixes;
}

void EngineRegistry::flushAll() {
    for (auto& [prefix, engine] : engines_) {
        LOG_INFO("[REGISTRY] Flushing engine at: " + prefix);
        engine->forceFlush();
    }
}

} // namespace kallisto::engine
