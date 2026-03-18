#pragma once

#include <string>
#include <memory>
#include "kallisto/secret_entry.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/tls_btree_manager.hpp"

namespace kallisto {

class RocksDBStorage;

class KallistoServer {
public:
    KallistoServer();
    ~KallistoServer();

    /**
     * @brief Stores a secret at a specific path.
     * 
     * @param path The path to store the secret at.
     * @param key The key of the secret.
     * @param value The value of the secret.
     * @return true if the secret was stored successfully, false otherwise.
     */
    bool putSecret(const std::string& path, const std::string& key, const std::string& value);

    /**
     * @brief Retrieves a secret.
     * 
     * @param path The path to retrieve the secret from.
     * @param key The key of the secret.
     * @return The value of the secret if found, empty string otherwise.
     */
    std::string getSecret(const std::string& path, const std::string& key);

    /**
     * @brief Deletes a secret.
     * 
     * @param path The path to delete the secret from.
     * @param key The key of the secret.
     * @return true if the secret was deleted successfully, false otherwise.
     */
    bool deleteSecret(const std::string& path, const std::string& key);

    enum class SyncMode {
        IMMEDIATE, // Sync to disk after every write (Safe, Slow)
        BATCH      // Sync only when threshold reached (Unsafe, Fast)
    };

    void setSyncMode(SyncMode mode);

    /**
     * @brief Manually triggers a disk flush.
     */
    void force_save();

private:
    std::unique_ptr<ShardedCuckooTable> storage;
    std::unique_ptr<TlsBTreeManager> path_index;
    std::unique_ptr<RocksDBStorage> rocksdb_persistence;

    // Default to safe mode
    SyncMode sync_mode = SyncMode::IMMEDIATE; 
    size_t unsaved_ops_count = 0;
    static constexpr size_t SYNC_THRESHOLD = 10000;
    void check_and_sync();

    std::string buildFullKey(const std::string& path, const std::string& key) const;

    // Internal helper to rebuild B-Tree after loading from disk
    void rebuildIndices(const std::vector<SecretEntry>& secrets);
};

} // namespace kallisto
