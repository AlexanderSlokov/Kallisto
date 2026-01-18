#include "kallisto/thread_local/thread_local.hpp"
#include "kallisto/event/dispatcher.hpp"
#include "kallisto/logger.hpp"

#include <mutex>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace kallisto {
namespace thread_local_storage {

/**
 * Thread-local data structure.
 * Each thread has one of these, stored in thread_local storage.
 */
struct ThreadLocalData {
    event::Dispatcher* dispatcher{nullptr};
    std::vector<ThreadLocalObjectPtr> slots;
    bool registered{false};
};

// The actual thread-local storage
static thread_local ThreadLocalData tls_data_;

/**
 * Slot implementation.
 */
class SlotImpl : public Slot {
public:
    explicit SlotImpl(Instance* parent, uint32_t index)
        : parent_(parent), index_(index) {}

    ThreadLocalObjectPtr get() override {
        if (!tls_data_.registered) {
            return nullptr;
        }
        if (index_ >= tls_data_.slots.size()) {
            return nullptr;
        }
        return tls_data_.slots[index_];
    }

    void set(Initializer initializer) override;  // Defined after InstanceImpl

private:
    friend class InstanceImpl;
    Instance* parent_;
    uint32_t index_;
    Initializer initializer_;
};

/**
 * Instance implementation.
 */
class InstanceImpl : public Instance {
public:
    InstanceImpl() : next_slot_index_(0), shutdown_(false) {
        debug("[TLS] Created ThreadLocal Instance");
    }

    ~InstanceImpl() override {
        shutdownGlobalThreading();
    }

    SlotPtr allocateSlot() override {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t index = next_slot_index_++;
        auto slot = std::make_unique<SlotImpl>(this, index);
        slots_.push_back(slot.get());
        
        debug("[TLS] Allocated slot " + std::to_string(index));
        return slot;
    }

    void registerThread(event::Dispatcher& dispatcher, bool is_main) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (tls_data_.registered) {
            warn("[TLS] Thread already registered");
            return;
        }

        tls_data_.dispatcher = &dispatcher;
        tls_data_.registered = true;
        tls_data_.slots.resize(next_slot_index_);

        // Initialize all existing slots for this thread
        for (auto* slot : slots_) {
            if (slot->initializer_ && slot->index_ < tls_data_.slots.size()) {
                tls_data_.slots[slot->index_] = slot->initializer_(dispatcher);
            }
        }

        registered_dispatchers_.push_back(&dispatcher);
        
        debug("[TLS] Registered thread '" + dispatcher.name() + 
              "' (main=" + std::to_string(is_main) + 
              ", total=" + std::to_string(registered_dispatchers_.size()) + ")");
    }

    void shutdownThread() override {
        if (!tls_data_.registered) return;

        debug("[TLS] Shutting down thread '" + tls_data_.dispatcher->name() + "'");
        
        // Clear slots in reverse order (dependencies)
        for (auto it = tls_data_.slots.rbegin(); it != tls_data_.slots.rend(); ++it) {
            it->reset();
        }
        tls_data_.slots.clear();
        tls_data_.dispatcher = nullptr;
        tls_data_.registered = false;
    }

    void shutdownGlobalThreading() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) return;
        
        shutdown_ = true;
        debug("[TLS] Global shutdown initiated");
        
        slots_.clear();
        registered_dispatchers_.clear();
    }

    size_t registeredThreadCount() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return registered_dispatchers_.size();
    }

    // Called by SlotImpl::set() to propagate to all registered threads
    void setOnAllThreads(SlotImpl* slot, Slot::Initializer initializer) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        slot->initializer_ = initializer;
        
        // Initialize on all already-registered threads
        for (auto* dispatcher : registered_dispatchers_) {
            dispatcher->post([slot, initializer, dispatcher]() {
                if (slot->index_ >= tls_data_.slots.size()) {
                    tls_data_.slots.resize(slot->index_ + 1);
                }
                tls_data_.slots[slot->index_] = initializer(*dispatcher);
            });
        }
    }

private:
    mutable std::mutex mutex_;
    std::atomic<uint32_t> next_slot_index_;
    std::atomic<bool> shutdown_;
    std::vector<SlotImpl*> slots_;  // Raw pointers, SlotImpl owned elsewhere
    std::vector<event::Dispatcher*> registered_dispatchers_;
};

// Deferred definition of SlotImpl::set
void SlotImpl::set(Initializer initializer) {
    static_cast<InstanceImpl*>(parent_)->setOnAllThreads(this, std::move(initializer));
}

// Factory function
InstancePtr createThreadLocalInstance() {
    return std::make_unique<InstanceImpl>();
}

} // namespace thread_local_storage
} // namespace kallisto
