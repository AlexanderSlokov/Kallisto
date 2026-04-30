#pragma once

#include "kallisto/secret_entry.hpp"
#include <concepts>
#include <optional>
#include <string>

namespace kallisto::engine {

/**
 * Compile-time contract for all secret engines.
 * Any class claiming to be an engine MUST satisfy this concept.
 * Used with static_assert to catch contract violations at build time.
 */
template<typename T>
concept ValidEngine = requires(T e,
                               const kallisto::SecretEntry& entry,
                               const std::string& path,
                               const std::string& key) {
    { e.put(entry) } -> std::convertible_to<bool>;
    { e.get(path, key) } -> std::same_as<std::optional<SecretEntry>>;
    { e.del(path, key) } -> std::convertible_to<bool>;
    { e.engineType() } -> std::convertible_to<std::string>;
};

} // namespace kallisto::engine
