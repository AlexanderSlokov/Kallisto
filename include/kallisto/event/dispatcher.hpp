#pragma once

#include <functional>
#include <memory>
#include <string>
#include <cstdint>

namespace kallisto {
namespace event {

// Forward declarations
class Timer;
using TimerPtr = std::unique_ptr<Timer>;

/**
 * Timer interface for delayed/periodic callbacks.
 */
class Timer {
public:
    virtual ~Timer() = default;
    
    /**
     * Enable the timer to fire after the specified duration.
     * @param duration_ms Milliseconds until timer fires
     */
    virtual void enableTimer(uint64_t duration_ms) = 0;
    
    /**
     * Disable a pending timer.
     */
    virtual void disableTimer() = 0;
    
    /**
     * @return true if timer is currently enabled
     */
    virtual bool enabled() const = 0;
};

/**
 * Event dispatcher interface - the core of each worker thread.
 * 
 * Each worker has its own Dispatcher with an independent epoll instance.
 * This enables true parallelism without lock contention on the event loop.
 * 
 * Inspired by Envoy's Event::Dispatcher.
 */
class Dispatcher {
public:
    virtual ~Dispatcher() = default;
    
    /**
     * Callback type for posted tasks.
     */
    using PostCb = std::function<void()>;
    
    /**
     * Callback type for file descriptor events.
     * @param events Bitmask of triggered events (EPOLLIN, EPOLLOUT, etc.)
     */
    using FdCb = std::function<void(uint32_t events)>;
    
    /**
     * Run the event loop. Blocks until exit() is called.
     * Internally calls epoll_wait in a loop.
     */
    virtual void run() = 0;
    
    /**
     * Signal the event loop to exit.
     * Thread-safe: can be called from any thread.
     */
    virtual void exit() = 0;
    
    /**
     * Post a callback to be executed on this dispatcher's thread.
     * Thread-safe: uses a lock-free MPSC queue internally.
     * 
     * @param callback The function to execute on the dispatcher thread
     */
    virtual void post(PostCb callback) = 0;
    
    /**
     * Create a timer that will execute on this dispatcher.
     * @param cb Callback to invoke when timer fires
     * @return A new timer instance
     */
    virtual TimerPtr createTimer(std::function<void()> cb) = 0;
    
    /**
     * Add a file descriptor to the epoll set.
     * @param fd File descriptor to watch
     * @param events Epoll events to watch for (EPOLLIN | EPOLLOUT, etc.)
     * @param cb Callback when events occur
     */
    virtual void addFd(int fd, uint32_t events, FdCb cb) = 0;
    
    /**
     * Modify events for an existing file descriptor.
     * @param fd File descriptor already in the epoll set
     * @param events New events to watch for
     */
    virtual void modifyFd(int fd, uint32_t events) = 0;
    
    /**
     * Remove a file descriptor from the epoll set.
     * @param fd File descriptor to remove
     */
    virtual void removeFd(int fd) = 0;
    
    /**
     * Check if current thread is the dispatcher's thread.
     * Used for assertions in debug builds.
     */
    virtual bool isThreadSafe() const = 0;
    
    /**
     * @return Name of this dispatcher (for logging/debugging)
     */
    virtual const std::string& name() const = 0;
};

using DispatcherPtr = std::unique_ptr<Dispatcher>;

/**
 * Factory for creating Dispatcher instances.
 */
class DispatcherFactory {
public:
    virtual ~DispatcherFactory() = default;
    
    /**
     * Create a new dispatcher with the given name.
     * @param name Identifier for logging (e.g., "worker_0")
     * @return A new dispatcher instance with its own epoll fd
     */
    virtual DispatcherPtr createDispatcher(const std::string& name) = 0;
};

using DispatcherFactoryPtr = std::unique_ptr<DispatcherFactory>;

} // namespace event
} // namespace kallisto
