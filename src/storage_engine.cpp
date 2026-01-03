#include "kallisto/storage_engine.hpp"
#include <cstring>

namespace kallisto {

StorageEngine::StorageEngine(const std::string& data_dir) : data_path(data_dir) {
    // Ensure data directory exists
    // Handle permissions gracefully - if /data/kallisto fails (e.g. permission denied)
    try {
        if (!std::filesystem::exists(data_path)) {
            std::filesystem::create_directories(data_path);
        }
    } catch (const std::exception& e) {
        // Just log error. Code should be deterministic - if something wrong then fail, and don't try to fix the misconfiguration for user.
        error("Failed to create data dir: " + data_path + ". Error: " + e.what());
    }
}

StorageEngine::~StorageEngine() = default;

void StorageEngine::write_string(std::ofstream& out, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    write_pod(out, len);
    out.write(str.data(), len);
}

std::string StorageEngine::read_string(std::ifstream& in) {
    uint32_t len;
    read_pod(in, len);
    
    std::string str(len, '\0');
    in.read(&str[0], len);
    return str;
}

bool StorageEngine::save_snapshot(const std::vector<SecretEntry>& secrets) {
    std::string full_path = data_path + "/" + snapshot_filename;
    std::ofstream out(full_path, std::ios::binary | std::ios::trunc);
    
    if (!out.is_open()) {
        error("Cannot open file for writing: " + full_path);
        return false;
    }

    // 1. Header
    write_pod(out, MAGIC_NUMBER);
    write_pod(out, VERSION);
    uint64_t count = secrets.size();
    write_pod(out, count);

    // 2. Body
    for (const auto& entry : secrets) {
        write_string(out, entry.path);
        write_string(out, entry.key);
        write_string(out, entry.value);
        
        // Metadata
        // Convert time_point to int64 for stable binary storage
        int64_t created_ts = std::chrono::system_clock::to_time_t(entry.created_at);
        write_pod(out, created_ts);
        write_pod(out, entry.ttl);
    }

    out.close();
    info("Snapshot saved to " + full_path + " (" + std::to_string(count) + " entries)");
    return true;
}

std::vector<SecretEntry> StorageEngine::load_snapshot() {
    std::vector<SecretEntry> secrets;
    std::string full_path = data_path + "/" + snapshot_filename;

    if (!std::filesystem::exists(full_path)) {
        warn("No snapshot found at " + full_path + ". Starting fresh.");
        return secrets; 
    }

    std::ifstream in(full_path, std::ios::binary);
    if (!in.is_open()) {
        error("Cannot open file for reading: " + full_path);
        return secrets;
    }

    // 1. Validate Header
    uint32_t magic, version;
    read_pod(in, magic);
    if (magic != MAGIC_NUMBER) {
        error("Corrupted file: Invalid Magic Number.");
        return secrets;
    }

    read_pod(in, version);
    if (version != VERSION) {
        error("Unsupported version: " + std::to_string(version));
        return secrets;
    }

    uint64_t count;
    read_pod(in, count);

    // 2. Read Body
    secrets.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        SecretEntry entry;
        entry.path = read_string(in);
        entry.key = read_string(in);
        entry.value = read_string(in);

        int64_t created_ts;
        read_pod(in, created_ts);
        entry.created_at = std::chrono::system_clock::from_time_t(created_ts);

        read_pod(in, entry.ttl);

        secrets.push_back(entry);
    }

    in.close();
    info("Loaded " + std::to_string(secrets.size()) + " secrets from disk.");
    return secrets;
}

} // namespace kallisto
