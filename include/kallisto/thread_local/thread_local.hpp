#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace kallisto {

// Forward declaration
namespace event {
class Dispatcher;
}

namespace thread_local_storage {

/**
 * Base class for objects stored in thread-local slots.
 * Derive from this to store custom data per-thread.
 */
class ThreadLocalObject {
public:
    virtual ~ThreadLocalObject() = default;
};

using ThreadLocalObjectPtr = std::shared_ptr<ThreadLocalObject>;

/**
 * A slot that holds thread-local data.
 * 
 * Each slot has a unique index and stores one ThreadLocalObject
 * per registered thread. Reading from a slot is ZERO-LOCK because
 * each thread accesses only its own copy.
 * 
 * Inspired by Envoy's ThreadLocal::Slot.
 */
class Slot {
public:
    virtual ~Slot() = default;
    
    /**
     * Get the thread-local object for the current thread.
     * ZERO-LOCK: Accesses thread_local storage directly.
     * 
     * @return The object for this slot on the current thread
     */
    virtual ThreadLocalObjectPtr get() = 0;
    
    /**
     * Set the thread-local object using an initializer.
     * The initializer is called once for each registered thread (including main).
     * 
     * @param initializer Function that creates the object for each thread.
     *                    Receives the thread's Dispatcher for context.
     */
    using Initializer = std::function<ThreadLocalObjectPtr(event::Dispatcher&)>;
    virtual void set(Initializer initializer) = 0;
    
    /**
     * Helper to get typed data.
     * @tparam T The derived type of ThreadLocalObject
     * @return Reference to the typed object
     */
    template<typename T>
    T& getTyped() {
        return *std::static_pointer_cast<T>(get());
    }
};

using SlotPtr = std::unique_ptr<Slot>;

/**
 * Thread-Local Storage manager.
 * 
 * Allocates slots and manages per-thread data. Each thread must
 * register itself before accessing slots.
 * 
 * Pattern:
 * 1. Main thread allocates slots
 * 2. Main thread registers itself
 * 3. Worker threads register themselves
 * 4. Slots can be read from any registered thread (zero-lock)
 * 
 * Inspired by Envoy's ThreadLocal::Instance.
 */
class Instance {
public:
    virtual ~Instance() = default;
    
    /**
     * Allocate a new slot.
     * Must be called from the main thread.
     * 
     * @return A new slot that can hold one object per thread
     */
    virtual SlotPtr allocateSlot() = 0;
    
    /**
     * Register the current thread with the TLS system.
     * Must be called once per thread before accessing slots.
     * 
     * @param dispatcher The thread's event dispatcher
     * @param is_main True if this is the main thread
     */
    virtual void registerThread(event::Dispatcher& dispatcher, bool is_main) = 0;
    
    /**
     * Shutdown TLS for the current thread.
     * Clears all slot data for this thread.
     */
    virtual void shutdownThread() = 0;
    
    /**
     * Begin global shutdown of the TLS system.
     * Called before destroying the Instance.
     */
    virtual void shutdownGlobalThreading() = 0;
    
    /**
     * @return Number of registered threads
     */
    virtual size_t registeredThreadCount() const = 0;
};

using InstancePtr = std::unique_ptr<Instance>;

/**
 * Create a new TLS instance.
 */
InstancePtr createThreadLocalInstance();

} // namespace thread_local_storage

// Convenient namespace alias
namespace tls = thread_local_storage;

} // namespace kallisto
