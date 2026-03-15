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
        std::lock_guard lock(const_cast<std::mutex&>(master_mutex_));
        tls_btree_ = master_btree_;
    }
    return tls_btree_;
}

bool TlsBTreeManager::update(const std::string& path) {
    auto new_master = createUpdatedMaster(path);
    if (!new_master) {
        return false;
    }

    dispatchUpdate(new_master);
    drain_garbage();
    return true;
}

std::shared_ptr<const BTreeIndex> TlsBTreeManager::createUpdatedMaster(const std::string& path) {
    std::lock_guard lock(master_mutex_);

    if (master_btree_->validatePath(path)) {
        return nullptr;
    }

    auto clone = std::make_shared<BTreeIndex>(*master_btree_);
    clone->insertPath(path);
    master_btree_ = clone;
    
    return clone;
}

void TlsBTreeManager::dispatchUpdate(std::shared_ptr<const BTreeIndex> new_master) {
    if (!workers_) {
        updateLocalSnapshot(new_master);
        return;
    }

    for (size_t i = 0; i < workers_->size(); ++i) {
        auto& worker = workers_->getWorker(i);
        if (&worker != nullptr && &worker.dispatcher() != nullptr) {
            worker.dispatcher().post([new_master]() {
                updateLocalSnapshot(new_master);
            });
        }
    }
}

void TlsBTreeManager::updateLocalSnapshot(std::shared_ptr<const BTreeIndex> new_master) {
    if (tls_btree_) {
        std::lock_guard gc_lock(gc_mutex_);
        gc_queue_.push_back(std::move(tls_btree_));
    }
    tls_btree_ = std::move(new_master);
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
