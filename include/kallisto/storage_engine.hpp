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
 * It uses a simple snapshot mechanism:
 * - Save: Dump all secrets to a binary file.
 * - Load: Read binary file and return vector of secrets.
 * 
 * Data Format:
 * [MagicNumber: 4 bytes]
 * [Version: 4 bytes]
 * [Count: 8 bytes]
 * [SecretEntry 1]
 * ...
 * [SecretEntry N]
 */
class StorageEngine {
public:
    StorageEngine(const std::string& data_dir = "kallisto/data");
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

    // Helper for serialization
    void write_string(std::ofstream& out, const std::string& str);
    std::string read_string(std::ifstream& in);
};

} // namespace kallisto
