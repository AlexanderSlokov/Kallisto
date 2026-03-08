#include "kallisto/tls_btree_manager.hpp"

namespace kallisto {

thread_local std::shared_ptr<const BTreeIndex> TlsBTreeManager::tls_btree_ = nullptr;
std::mutex TlsBTreeManager::gc_mutex_;
std::vector<std::shared_ptr<const BTreeIndex>> TlsBTreeManager::gc_queue_;

TlsBTreeManager::TlsBTreeManager(int degree, event::WorkerPool* workers)
    : workers_(workers) {
    master_btree_ = std::make_shared<const BTreeIndex>(degree);
    tls_btree_ = master_btree_;
}

std::shared_ptr<const BTreeIndex> TlsBTreeManager::get_local() const {
    if (!tls_btree_) {
        // Fallback for threads that haven't received an update yet, e.g. main thread
        // Or uninitialized workers. We do a quick lock to grab the master.
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(master_mutex_));
        tls_btree_ = master_btree_;
    }
    return tls_btree_;
}

bool TlsBTreeManager::update(const std::string& path) {
    std::shared_ptr<const BTreeIndex> new_master;

    {
        std::lock_guard<std::mutex> lock(master_mutex_);

        // Check if path already exists in current master
        if (master_btree_->validate_path(path)) {
            return true;
        }

        // Deep copy the master
        auto clone = std::make_shared<BTreeIndex>(*master_btree_);

        // Insert new path
        clone->insert_path(path);

        // Update master
        master_btree_ = clone;
        new_master = clone;
    }

    // Dispatch update to all workers
    if (workers_) {
        for (size_t i = 0; i < workers_->size(); ++i) {
            auto& worker = workers_->getWorker(i);
            if (&worker != nullptr && &worker.dispatcher() != nullptr) {
                worker.dispatcher().post([new_master]() {
                    // Current thread-local pointer goes into GC queue to avoid stall
                    if (tls_btree_) {
                        std::lock_guard<std::mutex> gc_lock(gc_mutex_);
                        gc_queue_.push_back(std::move(tls_btree_));
                    }
                    
                    // Atomic swap to new pointer
                    tls_btree_ = new_master;
                });
            }
        }
    } else {
        // For single-threaded or test scenarios
        if (tls_btree_) {
            std::lock_guard<std::mutex> gc_lock(gc_mutex_);
            gc_queue_.push_back(std::move(tls_btree_));
        }
        tls_btree_ = new_master;
    }

    // Try to cleanup GC queue off the hot path (or let a background thread do it)
    drain_garbage();
    return true;
}

void TlsBTreeManager::drain_garbage() {
    std::vector<std::shared_ptr<const BTreeIndex>> to_delete;
    {
        std::lock_guard<std::mutex> lock(gc_mutex_);
        to_delete.swap(gc_queue_);
    }
    // Shared pointers get deallocated here, off the event loop 
    // unless this is called by a worker thread (which we just did but on the writer thread).
    // The writer thread takes the hit of deallocation, not the reader thread.
    to_delete.clear(); 
}

} // namespace kallisto
