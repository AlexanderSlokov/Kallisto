#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace kallisto {

/**
 * SecretEntry defines the core data structure for a secret stored in Kallisto.
 */
struct SecretEntry {
    std::string key;
    std::string value;
    std::string path;
    std::chrono::system_clock::time_point created_at;
    uint32_t ttl; // in seconds
};

} // namespace kallisto
