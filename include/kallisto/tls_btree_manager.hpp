#pragma once

#include "kallisto/btree_index.hpp"
#include "kallisto/event/dispatcher.hpp"
#include "kallisto/event/worker.hpp"
#include <memory>
#include <mutex>
#include <vector>

namespace kallisto {

/**
 * TlsBTreeManager manages an Envoy-style RCU (Read-Copy-Update) synchronization 
 * for the BTreeIndex across multiple threads.
 * 
 * It eliminates the need for a global Read-Write Lock, allowing workers to perform
 * lock-free GET operations using thread-local snapshots. Writes (PUT/DELETE) use a 
 * Deep Copy mechanism and push updates to workers via Event Dispatchers.
 */
class TlsBTreeManager {
public:
    /**
     * @param degree The B-Tree degree
     * @param workers The worker pool to dispatch updates to
     */
    TlsBTreeManager(int degree, event::WorkerPool* workers);

    /**
     * Gets the thread-local lock-free snapshot.
     * @return Shared pointer to the thread-local BTreeIndex.
     */
    std::shared_ptr<const BTreeIndex> get_local() const;

    /**
     * Updates the global B-Tree with a new path and signals all workers.
     * @param path The path to insert.
     * @return true if insertion occurred or path already exists.
     */
    bool update(const std::string& path);

    /**
     * Optional: Trigger background GC if necessary.
     */
    void drain_garbage();

private:
    std::shared_ptr<const BTreeIndex> master_btree_;
    std::mutex master_mutex_;
    event::WorkerPool* workers_;

    // Thread-local pointer
    static thread_local std::shared_ptr<const BTreeIndex> tls_btree_;

    // Simple background GC queue for old snapshots to avoid destructor stall on hot paths
    static std::mutex gc_mutex_;
    static std::vector<std::shared_ptr<const BTreeIndex>> gc_queue_;
};

} // namespace kallisto
