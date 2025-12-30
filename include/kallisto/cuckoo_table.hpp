#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdlib>
#include "kallisto/secret_entry.hpp"
#include "kallisto/siphash.hpp"

namespace kallisto {

class CuckooTable {
public:
    /**
     * @param size The capacity of each of the two tables.
     */
    CuckooTable(size_t size = 1024);
    
    /**
     * Inserts a secret entry into the cuckoo table.
     * Uses the "kicking" mechanism to resolve collisions.
     * @return true if insertion was successful, false if a cycle was detected (full table).
     */
    bool insert(const std::string& key, const SecretEntry& entry);

    /**
     * Looks up an entry by key. O(1) worst-case.
     * @return The entry if found, std::nullopt otherwise.
     */
    std::optional<SecretEntry> lookup(const std::string& key) const;

    /**
     * Removes an entry by key.
     * @return true if entry was removed, false if not found.
     */
    bool remove(const std::string& key);

private:
    struct Bucket {
        bool occupied = false;
        std::string key;
        SecretEntry entry;
    };

    std::vector<Bucket> table_1;
    std::vector<Bucket> table_2;

    size_t capacity;
    const int max_displacements = 100;

    size_t hash_1(const std::string& key) const;
    size_t hash_2(const std::string& key) const;
    
    void rehash();

    SipHash hasher1;
    SipHash hasher2;
};

} // namespace kallisto
