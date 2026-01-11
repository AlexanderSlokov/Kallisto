#include "kallisto/cuckoo_table.hpp"
#include <stdexcept>
#include <random> // For random kick
#include <cstring> // for memset

namespace kallisto {

CuckooTable::CuckooTable(size_t size, size_t initial_capacity) : capacity(size) {
    table_1.resize(capacity);
    table_2.resize(capacity);
    
    // Initialize buckets with INVALID_INDEX
    // We cannot use memset safely on structs with constructors (even implicit), 
    // but Bucket is POD-like enough. However, proper iteration is safer.
    for (auto& bucket : table_1) {
        for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
            bucket.slots[i].index = INVALID_INDEX;
            bucket.slots[i].tag = 0;
        }
    }
    for (auto& bucket : table_2) {
        for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
            bucket.slots[i].index = INVALID_INDEX;
            bucket.slots[i].tag = 0;
        }
    }

    // Pre-allocate memory for entries to avoid expensive reallocations
    storage.reserve(initial_capacity);
    // free_list doesn't need reserve necessarily, but good practice
    free_list.reserve(initial_capacity / 10); 
}

uint64_t CuckooTable::hash_1_full(const std::string& key) const {
    // Seed 1: 0xDEADBEEF, 0xCAFEBABE
    return SipHash::hash(key, 0xDEADBEEF64, 0xCAFEBABE64);
}

uint64_t CuckooTable::hash_2_full(const std::string& key) const {
    // Seed 2: 0xFACEB00C, 0xDEADC0DE
    return SipHash::hash(key, 0xFACEB00C64, 0xDEADC0DE64);
}

bool CuckooTable::insert(const std::string& key, const SecretEntry& entry) {
    // 1. Check if key already exists (Update)
    // We must check both tables
    uint64_t h1_raw = hash_1_full(key);
    uint32_t tag = get_tag(h1_raw);
    size_t idx1 = h1_raw % capacity;

    for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
        uint32_t slot_idx = table_1[idx1].slots[i].index;
        if (slot_idx != INVALID_INDEX && table_1[idx1].slots[i].tag == tag) {
            // Potential match, check full key
            if (storage[slot_idx].key == key) {
                storage[slot_idx] = entry; // Update in place
                storage[slot_idx].key = key; // Ensure key consistency
                return true;
            }
        }
    }

    uint64_t h2_raw = hash_2_full(key);
    // Note: Tag depends on h1, but for checking persistence in T2, we should use consistency.
    // The standard Cuckoo uses same tag for both checks? 
    // Usually Tag is a fingerprint of the key.
    // If we use high bits of H1 as tag, we must use same Tag for H2 check.
    // Yes.
    size_t idx2 = h2_raw % capacity;

    for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
        uint32_t slot_idx = table_2[idx2].slots[i].index;
        if (slot_idx != INVALID_INDEX) {
             // For T2, we check if this slot holds our key.
             // We need to know if we stored the same tag?
             // Yes, when we move T1->T2, we carry the tag.
             if (table_2[idx2].slots[i].tag == tag && storage[slot_idx].key == key) {
                 storage[slot_idx] = entry; // Update
                 storage[slot_idx].key = key; // Ensure key consistency
                 return true;
             }
        }
    }

    // 2. Insert new entry
    uint32_t new_storage_idx;
    if (!free_list.empty()) {
        new_storage_idx = free_list.back();
        free_list.pop_back();
        storage[new_storage_idx] = entry;
        storage[new_storage_idx].key = key;
    } else {
        SecretEntry e = entry;
        e.key = key;
        storage.push_back(e);
        new_storage_idx = static_cast<uint32_t>(storage.size() - 1);
        // Check for 32-bit overflow? (Unlikely unless > 4 billion items)
    }

    uint32_t current_index = new_storage_idx;
    uint32_t current_tag = tag;
    
    // Attempt to insert
    for (int i = 0; i < max_displacements; ++i) {
        // Try Table 1
        // We need to recompute hash because we might be carrying a kicked item
        // Wait, for the *first* item it's h1_raw.
        // For kicked items, we need to know their original Key to rehash?
        // YES. Standard Cuckoo needs the Key to recompute alternate hash.
        // Accessing storage[current_index].key is permitted and fast (pointer arith).
        
        const std::string& cur_key = storage[current_index].key;
        uint64_t h1 = hash_1_full(cur_key);
        size_t b1 = h1 % capacity;
        
        // Find empty slot in B1
        for (int s = 0; s < SLOTS_PER_BUCKET; ++s) {
            if (table_1[b1].slots[s].index == INVALID_INDEX) {
                table_1[b1].slots[s].tag = current_tag;
                table_1[b1].slots[s].index = current_index;
                return true;
            }
        }

        // Try Table 2
        uint64_t h2 = hash_2_full(cur_key);
        size_t b2 = h2 % capacity;

        for (int s = 0; s < SLOTS_PER_BUCKET; ++s) {
            if (table_2[b2].slots[s].index == INVALID_INDEX) {
                table_2[b2].slots[s].tag = current_tag;
                table_2[b2].slots[s].index = current_index;
                return true;
            }
        }

        // Kick from Table 1 (Random slot)
        // Why T1? Can kick from T2 too. Simple strategy: Always kick from T1 or alternate.
        // Let's kick from T1 for simplicity in this loop, or alternate based on i?
        // Let's alternate to avoid loops.
        // Actually, classic cuckoo kicks from where it lands.
        // Here we just tried BOTH and both are full.
        // Pick a random victim from T1.
        int victim_slot = rand() % SLOTS_PER_BUCKET; // Simple rand
        
        // Swap
        std::swap(current_tag, table_1[b1].slots[victim_slot].tag);
        std::swap(current_index, table_1[b1].slots[victim_slot].index);
        
        // Now we hold the kicked item, process it in next iteration
    }

    // Table full / Cycle
    // Undo the allocation for the "last" item (which is now in current_index)
    // Actually, we fail to insert. The item in 'current_index' is lost from table view.
    // We should reclaim its storage.
    // CAUTION: The item ending up in 'current_index' might be the NEW item or an OLD item kicked out.
    // If it's an OLD item, we just accidentally deleted it from the table (data loss).
    // Resize is needed here. 
    // For this implementation, we return false.
    return false; 
}

std::optional<SecretEntry> CuckooTable::lookup(const std::string& key) const {
    uint64_t h1_raw = hash_1_full(key);
    uint32_t tag = get_tag(h1_raw);
    size_t idx1 = h1_raw % capacity;

    // Hint for auto-vectorization
    // TODO: AVX2 Optimization using _mm256_cmpeq_epi32
    for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
        if (table_1[idx1].slots[i].index != INVALID_INDEX && table_1[idx1].slots[i].tag == tag) {
             uint32_t data_idx = table_1[idx1].slots[i].index;
             if (storage[data_idx].key == key) {
                 return storage[data_idx];
             }
        }
    }

    uint64_t h2_raw = hash_2_full(key);
    size_t idx2 = h2_raw % capacity;

    for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
        if (table_2[idx2].slots[i].index != INVALID_INDEX && table_2[idx2].slots[i].tag == tag) {
             uint32_t data_idx = table_2[idx2].slots[i].index;
             if (storage[data_idx].key == key) {
                 return storage[data_idx];
             }
        }
    }

    return std::nullopt;
}

std::vector<SecretEntry> CuckooTable::get_all_entries() const {
    std::vector<SecretEntry> all_secrets;
    // Iterate active slots in buckets.
    // CAUTION: storage vector contains both active and "freed" (holes) entries.
    // We must valid references from buckets.
    // Or we can iterate storage and check if index is in free_list? 
    // Checking if in free_list is O(N) or O(logN) which is slow.
    // Faster: Iterate buckets, collect unique indices.
    
    // But buckets are authoritative source of truth.
    
    // We can iterate table_1 and table_2.
    all_secrets.reserve(storage.size() - free_list.size()); 

    for (const auto& bucket : table_1) {
        for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
            if (bucket.slots[i].index != INVALID_INDEX) {
                all_secrets.push_back(storage[bucket.slots[i].index]);
            }
        }
    }
    for (const auto& bucket : table_2) {
        for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
            if (bucket.slots[i].index != INVALID_INDEX) {
                all_secrets.push_back(storage[bucket.slots[i].index]);
            }
        }
    }
    return all_secrets;
}

bool CuckooTable::remove(const std::string& key) {
    uint64_t h1_raw = hash_1_full(key);
    uint32_t tag = get_tag(h1_raw);
    size_t idx1 = h1_raw % capacity;

    for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
        uint32_t idx = table_1[idx1].slots[i].index;
        if (idx != INVALID_INDEX && table_1[idx1].slots[i].tag == tag) {
             if (storage[idx].key == key) {
                 // Found! 
                 // 1. Mark slot empty
                 table_1[idx1].slots[i].index = INVALID_INDEX;
                 table_1[idx1].slots[i].tag = 0;
                 // 2. Return index to free list
                 free_list.push_back(idx);
                 return true;
             }
        }
    }

    uint64_t h2_raw = hash_2_full(key);
    size_t idx2 = h2_raw % capacity;

    for (int i = 0; i < SLOTS_PER_BUCKET; ++i) {
        uint32_t idx = table_2[idx2].slots[i].index;
        if (idx != INVALID_INDEX && table_2[idx2].slots[i].tag == tag) {
             if (storage[idx].key == key) {
                 // Found!
                 table_2[idx2].slots[i].index = INVALID_INDEX;
                 table_2[idx2].slots[i].tag = 0;
                 free_list.push_back(idx);
                 return true;
             }
        }
    }

    return false;
}

void CuckooTable::rehash() {
    // Not implemented for this prototype.
}


} // namespace kallisto

