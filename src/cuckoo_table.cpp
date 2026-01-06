#include "kallisto/cuckoo_table.hpp"
#include <stdexcept>

namespace kallisto {

CuckooTable::CuckooTable(size_t size) : capacity(size) {
    table_1.resize(capacity);
    table_2.resize(capacity);
}

size_t CuckooTable::hash_1(const std::string& key) const {
    // Seed 1: 0xDEADBEEF, 0xCAFEBABE
    return SipHash::hash(key, 0xDEADBEEF64, 0xCAFEBABE64) % capacity;
}

size_t CuckooTable::hash_2(const std::string& key) const {
    // Seed 2: 0xFACEB00C, 0xDEADC0DE
    return SipHash::hash(key, 0xFACEB00C64, 0xDEADC0DE64) % capacity;
}

bool CuckooTable::insert(const std::string& key, const SecretEntry& entry) {
    // Check for update first
    size_t h1 = hash_1(key);
    if (table_1[h1].occupied && table_1[h1].key == key) {
        table_1[h1].entry = entry;
        return true;
    }

    size_t h2 = hash_2(key);
    if (table_2[h2].occupied && table_2[h2].key == key) {
        table_2[h2].entry = entry;
        return true;
    }

    std::string current_key = key;
    SecretEntry current_entry = entry;

    for (int i = 0; i < max_displacements; ++i) {
        size_t h1 = hash_1(current_key);
        if (!table_1[h1].occupied) {
            table_1[h1] = {true, current_key, current_entry};
            return true;
        }

        // Kick out existing entry in table_1
        std::swap(current_key, table_1[h1].key);
        std::swap(current_entry, table_1[h1].entry);

        size_t h2 = hash_2(current_key);
        if (!table_2[h2].occupied) {
            table_2[h2] = {true, current_key, current_entry};
            return true;
        }

        // Kick out existing entry in table_2
        std::swap(current_key, table_2[h2].key);
        std::swap(current_entry, table_2[h2].entry);
    }

    // If we reach here, we need to rehash (for prototype, we just return false)
    // In a real implementation, rehash() would be called.
    return false; 
}

std::optional<SecretEntry> CuckooTable::lookup(const std::string& key) const {
    size_t h1 = hash_1(key);
    if (table_1[h1].occupied && table_1[h1].key == key) {
        return table_1[h1].entry;
    }

    size_t h2 = hash_2(key);
    if (table_2[h2].occupied && table_2[h2].key == key) {
        return table_2[h2].entry;
    }

    return std::nullopt;
}

std::vector<SecretEntry> CuckooTable::get_all_entries() const {
    std::vector<SecretEntry> all_secrets;
    // Pre-allocate to avoid resize overhead, at least size/2
    all_secrets.reserve(capacity); 

    for (const auto& bucket : table_1) {
        if (bucket.occupied) {
            all_secrets.push_back(bucket.entry);
        }
    }
    for (const auto& bucket : table_2) {
        if (bucket.occupied) {
            all_secrets.push_back(bucket.entry);
        }
    }
    return all_secrets;
}

bool CuckooTable::remove(const std::string& key) {
    size_t h1 = hash_1(key);
    if (table_1[h1].occupied && table_1[h1].key == key) {
        table_1[h1].occupied = false;
        return true;
    }

    size_t h2 = hash_2(key);
    if (table_2[h2].occupied && table_2[h2].key == key) {
        table_2[h2].occupied = false;
        return true;
    }

    return false;
}

void CuckooTable::rehash() {
    // Not implemented for this prototype scope, but would involve 
    // doubling capacity and re-inserting all items.
}

} // namespace kallisto
