#include "kallisto/event/dispatcher.hpp"
#include "kallisto/logger.hpp"
#include "kallisto/thread_local/thread_local.hpp"

#include <atomic>
#include <mutex>
#include <ranges>
#include <vector>
#include <stdexcept>
#include <cassert>

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
static thread_local ThreadLocalData tls_data;

/**
 * Slot implementation.
 */
class SlotImpl : public Slot {
public:
  explicit SlotImpl(Instance* parent, uint32_t index) : parent_(parent), index_(index) {
    assert(parent != nullptr && "Slot must belong to a valid TLS Instance");
  }

  ThreadLocalObjectPtr get() override {
    if (!isThreadRegistered()) {
      return nullptr;
    }
    if (isIndexOutOfBounds()) {
      return nullptr;
    }
    return tls_data.slots[index_];
  }

  void set(Initializer initializer) override; // Defined after InstanceImpl

private:
  bool isThreadRegistered() const {
    return tls_data.registered;
  }
  
  bool isIndexOutOfBounds() const {
    return index_ >= tls_data.slots.size();
  }

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

  ~InstanceImpl() override { shutdownGlobalThreading(); }

  SlotPtr allocateSlot() override {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t slot_index = fetchAndIncrementSlotIndex();
    auto new_slot = std::make_unique<SlotImpl>(this, slot_index);
    slots_.push_back(new_slot.get());

    debug("[TLS] Allocated slot " + std::to_string(slot_index));
    return new_slot;
  }

  void registerThread(event::Dispatcher& dispatcher, bool is_main) override {
    std::lock_guard<std::mutex> lock(mutex_);

    validateThreadNotRegistered();
    initializeThreadLocalState(dispatcher);
    populateExistingSlotsForThread(dispatcher);
    
    registered_dispatchers_.push_back(&dispatcher);

    debug("[TLS] Registered thread '" + dispatcher.name() + "' (main=" + std::to_string(is_main) +
          ", total=" + std::to_string(registered_dispatchers_.size()) + ")");
  }

  void shutdownThread() override {
    if (!tls_data.registered) {
      return;
    }

    debug("[TLS] Shutting down thread '" + tls_data.dispatcher->name() + "'");
    clearThreadLocalResources();
  }

  void shutdownGlobalThreading() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) {
      return; // Safe idempotence
    }

    shutdown_ = true;
    debug("[TLS] Global shutdown initiated");

    slots_.clear();
    registered_dispatchers_.clear();
  }

  size_t registeredThreadCount() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return registered_dispatchers_.size();
  }

  void setOnAllThreads(SlotImpl* slot, Slot::Initializer initializer) {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(slot != nullptr && "Cannot propagate null slot");

    slot->initializer_ = initializer;
    broadcastInitializerToThreads(slot, std::move(initializer));
  }

private:
  uint32_t fetchAndIncrementSlotIndex() {
      return next_slot_index_++;
  }

  void validateThreadNotRegistered() const {
    if (tls_data.registered) {
      error("[TLS] Thread already registered");
      throw std::logic_error("Thread is already registered with TLS instance");
    }
  }

  void initializeThreadLocalState(event::Dispatcher& dispatcher) const {
    tls_data.dispatcher = &dispatcher;
    tls_data.registered = true;
    tls_data.slots.resize(next_slot_index_);
  }

  void populateExistingSlotsForThread(event::Dispatcher& dispatcher) const {
    for (auto* slot : slots_) {
      if (slot->initializer_ && slot->index_ < tls_data.slots.size()) {
        tls_data.slots[slot->index_] = slot->initializer_(dispatcher);
      }
    }
  }

  void clearThreadLocalResources() const {
    // Clear slots in reverse order to respect construction dependencies
    for (auto & slot : std::views::reverse(tls_data.slots)) {
      slot.reset();
    }
    tls_data.slots.clear();
    tls_data.dispatcher = nullptr;
    tls_data.registered = false;
  }

  void broadcastInitializerToThreads(SlotImpl* slot, Slot::Initializer initializer) const {
    for (auto* dispatcher : registered_dispatchers_) {
      auto captured_initializer = initializer; 
      dispatcher->post([slot, captured_initializer, dispatcher]() {
        resizeThreadSlotsIfNecessary(slot->index_);
        tls_data.slots[slot->index_] = captured_initializer(*dispatcher);
      });
    }
  }

  static void resizeThreadSlotsIfNecessary(uint32_t required_index) {
    if (required_index >= tls_data.slots.size()) {
      tls_data.slots.resize(required_index + 1); // Protect against OOB when a new slot is allocated and set is called asynchronously
    }
  }

  mutable std::mutex mutex_;
  std::atomic<uint32_t> next_slot_index_;
  std::atomic<bool> shutdown_;
  std::vector<SlotImpl*> slots_; // Raw pointers, SlotImpl owned elsewhere
  std::vector<event::Dispatcher*> registered_dispatchers_;
};

// Deferred definition of SlotImpl::set
void SlotImpl::set(Initializer initializer) {
  static_cast<InstanceImpl*>(parent_)->setOnAllThreads(this, std::move(initializer));
}

// Factory function
InstancePtr createThreadLocalInstance() { return std::make_unique<InstanceImpl>(); }

} // namespace thread_local_storage
} // namespace kallisto
