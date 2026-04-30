#pragma once

#include "kallisto/secret_entry.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/tls_btree_manager.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <optional>

namespace kallisto {

class RocksDBStorage;

class KallistoCore {
public:
  KallistoCore(const std::string& db_path = "/var/lib/kallisto/data");
  ~KallistoCore();

  // Prevent copying
  KallistoCore(const KallistoCore&) = delete;
  KallistoCore& operator=(const KallistoCore&) = delete;

  /**
   * @brief Stores a secret at a specific path.
   * @param path The path to store the secret at.
   * @param key The key of the secret.
   * @param value The value of the secret.
   * @param ttl_secs Time to live in seconds. Default boundary is safely set to 3600s.
   * @return true if the secret was stored successfully.
   */
  bool put(const std::string& path, const std::string& key, const std::string& value,
           uint64_t ttl_secs = 3600);

  /**
   * @brief Retrieves a secret. Includes logic for Cache-Miss fallback to RocksDB.
   * @return The secret entry wrapped in std::optional, or std::nullopt if not found.
   */
  std::optional<SecretEntry> get(const std::string& path, const std::string& key);

  /**
   * @brief Deletes a secret.
   */
  bool del(const std::string& path, const std::string& key);

  enum class SyncMode {
    IMMEDIATE, // Sync to disk after every write (Safe, Slow)
    BATCH      // Sync only when threshold reached (Unsafe, Fast)
  };

  /**
   * @brief Thread-safe toggle for sync mode.
   */
  void changeSyncMode(SyncMode mode);
  SyncMode getSyncMode() const;

  /**
   * @brief Manually triggers a disk flush.
   */
  void forceFlush();

private:
  std::unique_ptr<ShardedCuckooTable> storage_;
  std::unique_ptr<TlsBTreeManager> path_index_;
  std::unique_ptr<RocksDBStorage> rocksdb_persistence_;

  // Thread-safe state for lock-free hot path (OS-Level Constraint)
  std::atomic<SyncMode> sync_mode_{SyncMode::IMMEDIATE};
  std::atomic<size_t> unsaved_ops_count_{0};

  static constexpr size_t default_cuckoo_size = 2097152; // 2^21 slots
  static constexpr int default_btree_degree = 100;
  static constexpr size_t sync_threshold = 100000;

  // Internal helpers
  void checkAndSync();
  std::string buildFullKey(const std::string& path, const std::string& key) const;
  void rebuildIndices(const std::vector<SecretEntry>& secrets);
};

} // namespace kallisto
