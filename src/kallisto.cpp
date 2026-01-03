#include "kallisto/kallisto.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {

KallistoServer::KallistoServer() {
    // TODO: implement ENV to change the size of initial Cuckoo Table.
    // We plan to benchmark 10,000 items. 
    // Cuckoo Hashing typically degrades if the load factor is above 50% (leads to cycles/infinite loops).
    // Capacity of 2 tables with size 16384 is 32,768 slots.
    // Load Factor = 10,000 / 32,768 ≈ 30% (should be safe).
    storage = std::make_unique<CuckooTable>(16384);
    path_index = std::make_unique<BTreeIndex>(5);
    persistence = std::make_unique<StorageEngine>();

    // Recover state from disk at /data/kallisto/
    auto secrets = persistence->load_snapshot();
    if (!secrets.empty()) {
        rebuild_indices(secrets);
    }
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
    bool result = storage->insert(full_key, entry);

    // 4. Persist to Disk (In prototype, we sync on every write for safety)
    if (result) {
        persistence->save_snapshot(storage->get_all_entries());
    }
    
    return result;
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
    bool result = storage->remove(full_key);
    
    if (result) {
        persistence->save_snapshot(storage->get_all_entries());
    }
    
    return result;
}

void KallistoServer::rebuild_indices(const std::vector<SecretEntry>& secrets) {
    info("[RECOVERY] Rebuilding state from " + std::to_string(secrets.size()) + " entries...");
    for (const auto& entry : secrets) {
        // 1. Rebuild B-Tree
        path_index->insert_path(entry.path);
        
        // 2. Re-populate Cuckoo Table
        std::string full_key = build_full_key(entry.path, entry.key);
        storage->insert(full_key, entry);
    }
    info("[RECOVERY] Completed.");
}

} // namespace kallisto
