#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "kallisto/secret_entry.hpp"
#include "kallisto/logger.hpp"

namespace kallisto {

/**
 * StorageEngine handles the local persistence of secrets.
 * Instead of creating thousands of small files (slow because of inode overhead), we dump the entire state to a single optimized binary file.
 * File Format:
 * [MagicNumber: 4 bytes] (KALL)
 * [Version: 4 bytes]     (0x01 - Prevent format upgrade from breaking old code)
 * [Count: 8 bytes]       (Number of entries)
 * [Entry 1 Size: 4 bytes] [Entry 1 Data]
 * ...
 * [BODY (Continuous Data) ]
 * +-----------------------+
 * | [Len: 4b] [Path...]   | -> Entry 1: Path
 * | [Len: 4b] [Key...]    | -> Entry 1: Key
 * | [Len: 4b] [Value...]  | -> Entry 1: Value
 * | [TTL: 4b] [Created:8b]| -> Entry 1: Metadata
 * +-----------------------+
 * |    ... Entry 2 ...    |
 * +-----------------------+
 */
class StorageEngine {
public:
    StorageEngine(const std::string& data_dir = "/data/kallisto");
    ~StorageEngine();

    /**
     * Saves a snapshot of all current secrets to disk.
     * @param secrets List of all active secret entries.
     * @return true if successful.
     */
    bool save_snapshot(const std::vector<SecretEntry>& secrets);

    /**
     * Loads secrets from the snapshot file.
     * @return List of secrets loaded from disk.
     */
    std::vector<SecretEntry> load_snapshot();

private:
    std::string data_path;
    const std::string snapshot_filename = "kallisto.db";
    const uint32_t MAGIC_NUMBER = 0x4B414C4C; // 'KALL'
    const uint32_t VERSION = 1;

    // Helpers for binary serialization
    template<typename T>
    void write_pod(std::ofstream& out, const T& value) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template<typename T>
    void read_pod(std::ifstream& in, T& value) {
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
    }

    void write_string(std::ofstream& out, const std::string& str);
    std::string read_string(std::ifstream& in);
};

} // namespace kallisto
