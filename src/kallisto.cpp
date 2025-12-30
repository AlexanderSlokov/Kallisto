#include "kallisto/kallisto.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {

KallistoServer::KallistoServer() {
    // Cần cho phép truyền ENV để thay đổi kích thước Cuckoo Table và B-Tree
    storage = std::make_unique<CuckooTable>(2048);
    path_index = std::make_unique<BTreeIndex>(5);
}

KallistoServer::~KallistoServer() = default;

std::string KallistoServer::build_full_key(const std::string& path, const std::string& key) const {
    return path + "/" + key;
}

bool KallistoServer::put_secret(const std::string& path, const std::string& key, const std::string& value) {
    debug("Action: PUT path=" + path + " key=" + key);
    
    // 1. Ensure path exists in path index
    path_index->insert_path(path);
    
    // 2. Prepare secret entry
    SecretEntry entry;
    entry.key = key;
    entry.value = value;
    entry.path = path;
    entry.created_at = std::chrono::system_clock::now();
    entry.ttl = 3600; // Default TTL

    // 3. Store in Cuckoo Table
    std::string full_key = build_full_key(path, key);
    return storage->insert(full_key, entry);
}

std::string KallistoServer::get_secret(const std::string& path, const std::string& key) {
    info("[KallistoServer] Request: GET path=" + path + " key=" + key);
    
    // Step 1: Validate Path using B-Tree
    debug("[B-TREE] Validating path...");
    if (!path_index->validate_path(path)) {
        error("[B-TREE] Path validation failed: " + path);
        return "";
    }
    debug("[B-TREE] Path validated at: " + path);

    // Step 2: Secure Lookup in Cuckoo Table
    debug("[CUCKOO] Looking up secret...");
    std::string full_key = build_full_key(path, key);
    auto entry = storage->lookup(full_key);
    
    if (entry) {
        info("[CUCKOO] HIT! Value retrieved.");
        return entry->value;
    } else {
        warn("[CUCKOO] MISS! Secret not found.");
        return "";
    }
}

bool KallistoServer::delete_secret(const std::string& path, const std::string& key) {
    std::string full_key = build_full_key(path, key);
    return storage->remove(full_key);
}

} // namespace kallisto
