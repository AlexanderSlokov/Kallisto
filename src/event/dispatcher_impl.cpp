#include "kallisto/event/dispatcher.hpp"
#include "kallisto/logger.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <queue>
#include <atomic>

namespace kallisto {
namespace event {

namespace {
    constexpr int MAX_EVENTS = 64;
    constexpr int EPOLL_TIMEOUT_MS = 100;  // Wake up periodically to check exit flag
}

/**
 * Timer implementation using timerfd.
 */
class TimerImpl : public Timer {
public:
    TimerImpl(int epoll_fd, std::function<void()> cb)
        : epoll_fd_(epoll_fd), callback_(std::move(cb)), enabled_(false) {
        // Create timerfd
        timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timer_fd_ < 0) {
            error("[TIMER] Failed to create timerfd: " + std::string(strerror(errno)));
            return;
        }
    }

    ~TimerImpl() override {
        if (timer_fd_ >= 0) {
            // Remove from epoll before closing
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, timer_fd_, nullptr);
            close(timer_fd_);
        }
    }

    void enableTimer(uint64_t duration_ms) override {
        struct itimerspec its{};
        its.it_value.tv_sec = duration_ms / 1000;
        its.it_value.tv_nsec = (duration_ms % 1000) * 1000000;
        its.it_interval.tv_sec = 0;  // One-shot
        its.it_interval.tv_nsec = 0;

        if (timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
            error("[TIMER] Failed to set timer: " + std::string(strerror(errno)));
            return;
        }

        enabled_ = true;
    }

    void disableTimer() override {
        struct itimerspec its{};  // Zero = disable
        timerfd_settime(timer_fd_, 0, &its, nullptr);
        enabled_ = false;
    }

    bool enabled() const override {
        return enabled_;
    }

    int fd() const { return timer_fd_; }
    
    void fire() {
        // Read the timerfd to clear the event
        uint64_t expirations;
        [[maybe_unused]] ssize_t s = read(timer_fd_, &expirations, sizeof(expirations));
        enabled_ = false;
        if (callback_) {
            callback_();
        }
    }

private:
    int epoll_fd_;
    int timer_fd_{-1};
    std::function<void()> callback_;
    std::atomic<bool> enabled_;
};

/**
 * epoll-based Dispatcher implementation.
 */
class DispatcherImpl : public Dispatcher {
public:
    explicit DispatcherImpl(const std::string& name)
        : name_(name), running_(false), exit_requested_(false) {
        
        // Create epoll instance
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            error("[DISPATCHER] Failed to create epoll: " + std::string(strerror(errno)));
            return;
        }

        // Create eventfd for cross-thread wakeup (used by post())
        wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeup_fd_ < 0) {
            error("[DISPATCHER] Failed to create eventfd: " + std::string(strerror(errno)));
            close(epoll_fd_);
            return;
        }

        // Add wakeup_fd to epoll
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = wakeup_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev) < 0) {
            error("[DISPATCHER] Failed to add wakeup fd to epoll");
        }

        // Record the thread ID after run() starts
        debug("[DISPATCHER] Created dispatcher '" + name_ + "'");
    }

    ~DispatcherImpl() override {
        if (wakeup_fd_ >= 0) close(wakeup_fd_);
        if (epoll_fd_ >= 0) close(epoll_fd_);
    }

    void run() override {
        run_thread_id_ = std::this_thread::get_id();
        running_ = true;
        exit_requested_ = false;

        debug("[DISPATCHER] '" + name_ + "' entering event loop");

        struct epoll_event events[MAX_EVENTS];

        while (!exit_requested_) {
            int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
            
            if (nfds < 0) {
                if (errno == EINTR) continue;  // Interrupted by signal
                error("[DISPATCHER] epoll_wait error: " + std::string(strerror(errno)));
                break;
            }

            // Process events
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                uint32_t event_mask = events[i].events;

                if (fd == wakeup_fd_) {
                    // Wakeup event - drain the eventfd and process posted callbacks
                    drainWakeup();
                    runPostedCallbacks();
                } else if (auto it = timers_.find(fd); it != timers_.end()) {
                    // Timer event
                    it->second->fire();
                } else if (auto it = fd_callbacks_.find(fd); it != fd_callbacks_.end()) {
                    // File descriptor event
                    it->second(event_mask);
                }
            }

            // Also run posted callbacks after each iteration (in case wakeup was missed)
            runPostedCallbacks();
        }

        running_ = false;
        debug("[DISPATCHER] '" + name_ + "' exited event loop");
    }

    void exit() override {
        exit_requested_ = true;
        wakeup();  // Wake up epoll_wait if it's blocking
    }

    void post(PostCb callback) override {
        {
            std::lock_guard<std::mutex> lock(post_mutex_);
            post_queue_.push(std::move(callback));
        }
        wakeup();
    }

    TimerPtr createTimer(std::function<void()> cb) override {
        auto timer = std::make_unique<TimerImpl>(epoll_fd_, std::move(cb));
        
        // Add timer fd to epoll
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = timer->fd();
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer->fd(), &ev);
        
        // Track timer for callback dispatch
        timers_[timer->fd()] = timer.get();
        
        return timer;
    }

    void addFd(int fd, uint32_t events, FdCb cb) override {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            error("[DISPATCHER] Failed to add fd " + std::to_string(fd) + ": " + strerror(errno));
            return;
        }
        
        fd_callbacks_[fd] = std::move(cb);
    }

    void modifyFd(int fd, uint32_t events) override {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
            error("[DISPATCHER] Failed to modify fd " + std::to_string(fd) + ": " + strerror(errno));
        }
    }

    void removeFd(int fd) override {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        fd_callbacks_.erase(fd);
    }

    bool isThreadSafe() const override {
        // Empty thread_id means run() hasn't been called yet
        if (run_thread_id_ == std::thread::id{}) {
            return true;
        }
        return std::this_thread::get_id() == run_thread_id_;
    }

    const std::string& name() const override {
        return name_;
    }

private:
    void wakeup() {
        uint64_t val = 1;
        [[maybe_unused]] ssize_t s = write(wakeup_fd_, &val, sizeof(val));
    }

    void drainWakeup() {
        uint64_t val;
        while (read(wakeup_fd_, &val, sizeof(val)) > 0) {
            // Drain all pending wakeups
        }
    }

    void runPostedCallbacks() {
        std::queue<PostCb> to_run;
        {
            std::lock_guard<std::mutex> lock(post_mutex_);
            std::swap(to_run, post_queue_);
        }
        
        while (!to_run.empty()) {
            auto& cb = to_run.front();
            if (cb) cb();
            to_run.pop();
        }
    }

    std::string name_;
    int epoll_fd_{-1};
    int wakeup_fd_{-1};
    
    std::atomic<bool> running_;
    std::atomic<bool> exit_requested_;
    std::thread::id run_thread_id_;
    
    // Posted callbacks (protected by mutex, but run on dispatcher thread)
    std::mutex post_mutex_;
    std::queue<PostCb> post_queue_;
    
    // File descriptor callbacks
    std::unordered_map<int, FdCb> fd_callbacks_;
    
    // Timer tracking (we store raw pointers; TimerImpl stored elsewhere)
    std::unordered_map<int, TimerImpl*> timers_;
};

/**
 * Factory implementation.
 */
class DispatcherFactoryImpl : public DispatcherFactory {
public:
    DispatcherPtr createDispatcher(const std::string& name) override {
        return std::make_unique<DispatcherImpl>(name);
    }
};

} // namespace event

// Factory function
event::DispatcherFactoryPtr createDispatcherFactory() {
    return std::make_unique<event::DispatcherFactoryImpl>();
}

} // namespace kallisto
