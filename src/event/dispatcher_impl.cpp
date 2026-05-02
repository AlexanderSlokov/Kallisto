#include "kallisto/event/dispatcher.hpp"
#include "kallisto/logger.hpp"

#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <queue>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kallisto {
namespace event {

namespace {
constexpr int max_events = 64;
constexpr int epoll_timeout_ms = 100; // periodically check exit flag
}

/**
 * Timer implementation using timerfd.
 * This class translates time durations into Linux timerfd constructs.
 * It is fully isolated from Dispatcher/epoll structures.
 */
class TimerImpl : public Timer {
public:
  explicit TimerImpl(std::function<void()> cb, std::function<void(int)> on_destroy)
    : callback_(std::move(cb)), on_destroy_(std::move(on_destroy)), enabled_(false) {
    
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ < 0) {
      error("[TIMER] Failed to create timerfd: " + std::string(strerror(errno)));
      throw std::runtime_error("timerfd_create failed: " + std::string(strerror(errno)));
    }
  }

  ~TimerImpl() override {
    if (timer_fd_ >= 0) {
      // Notify parent dispatcher to clean up tracking resources (callbacks, maps, etc.)
      if (on_destroy_) {
        on_destroy_(timer_fd_);
      }
      // Rely on Linux OS behavior: closing an FD automatically removes it from all epoll sets.
      close(timer_fd_);
    }
  }

  void enableTimer(uint64_t duration_ms) override {
    struct itimerspec its = msToItimerSpec(duration_ms);

    if (timerfd_settime(timer_fd_, 0, &its, nullptr) < 0) {
      error("[TIMER] Failed to set timer: " + std::string(strerror(errno)));
      return;
    }

    enabled_ = true;
  }

  void disableTimer() override {
    struct itimerspec its{}; // Initializing to zero disables the timer
    timerfd_settime(timer_fd_, 0, &its, nullptr);
    enabled_ = false;
  }

  bool enabled() const override { return enabled_; }

  int fd() const { return timer_fd_; }

  void fire() {
    clearTimerEvent();
    enabled_ = false;
    invokeCallback();
  }

private:
  static constexpr uint64_t ms_in_sec = 1000;
  static constexpr uint64_t ns_in_ms = 1000000;

  struct itimerspec msToItimerSpec(uint64_t duration_ms) const {
    struct itimerspec its{};
    its.it_value.tv_sec = duration_ms / ms_in_sec;
    its.it_value.tv_nsec = (duration_ms % ms_in_sec) * ns_in_ms;
    its.it_interval.tv_sec = 0; // One-shot behavior
    its.it_interval.tv_nsec = 0;
    return its;
  }

  void clearTimerEvent() const {
    uint64_t expirations;
    // Reading from a timerfd resets it and clears the event inside epoll.
    [[maybe_unused]] ssize_t s = read(timer_fd_, &expirations, sizeof(expirations));
  }

  void invokeCallback() const {
    if (callback_) {
      callback_();
    }
  }

  int timer_fd_{-1};
  std::function<void()> callback_;
  std::function<void(int)> on_destroy_;
  std::atomic<bool> enabled_;
};

/**
 * epoll-based Dispatcher implementation.
 */
class DispatcherImpl : public Dispatcher {
public:
  explicit DispatcherImpl(const std::string& name)
    : name_(name), running_(false), exit_requested_(false) {
    
    epoll_fd_ = createEpollInstance();
    wakeup_fd_ = createWakeupEventFd();
    registerWakeupFd(epoll_fd_, wakeup_fd_);
    
    debug("[DISPATCHER] Created dispatcher '" + name_ + "'");
  }

  ~DispatcherImpl() override {
    if (wakeup_fd_ >= 0) {
      close(wakeup_fd_);
    }
    if (epoll_fd_ >= 0) {
      close(epoll_fd_);
    }
  }

  void run() override {
    run_thread_id_ = std::this_thread::get_id();
    running_ = true;
    exit_requested_ = false;

    debug("[DISPATCHER] '" + name_ + "' entering event loop");

    while (!exit_requested_) {
      processImmediateTasks();
      waitForAndDispatchEvents();
      applyDeferredMutations();
    }

    running_ = false;
    debug("[DISPATCHER] '" + name_ + "' exited event loop");
  }

  void exit() override {
    exit_requested_ = true;
    triggerWakeup(); 
  }

  void post(PostCb callback) override {
    {
      std::lock_guard<std::mutex> lock(post_mutex_);
      post_queue_.push(std::move(callback));
    }

    // Edge Case(Wait-Sleep Anomaly):
    // Only pay the cost of a syscall 'write' if the event loop is actually sleeping. If the loop is actively processing, it will naturally pick up the newly posted task.
    if (is_sleeping_.load(std::memory_order_acquire)) {
      triggerWakeup();
    }
  }

  TimerPtr createTimer(std::function<void()> cb) override {
    // Inject a cleanup hook so we do not leak Timer callbacks inside fd_callbacks_ when a timer is destructed.
    auto timer = std::make_unique<TimerImpl>(std::move(cb), [this](int fd) {
      this->removeFd(fd);
    });

    int fd = timer->fd();
    
    // Unify handling: timers are treated exactly like regular sockets.
    addFd(fd, EPOLLIN, [t = timer.get()](uint32_t) {
      t->fire();
    });

    return timer;
  }

  void addFd(int fd, uint32_t events, FdCb cb) override {
    if (!isThreadSafe()) {
      post([this, fd, events, cb = std::move(cb)]() mutable {
        addFd(fd, events, std::move(cb));
      });
      return;
    }

    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
      error("[DISPATCHER] Failed to add fd " + std::to_string(fd) + ": " + strerror(errno));
      return;
    }

    if (is_iterating_) {
      pending_adds_.emplace_back(fd, std::move(cb));
    } else {
      fd_callbacks_[fd] = std::move(cb);
    }
  }

  void modifyFd(int fd, uint32_t events) override {
    if (!isThreadSafe()) {
      post([this, fd, events]() {
        modifyFd(fd, events);
      });
      return;
    }

    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
      error("[DISPATCHER] Failed to modify fd " + std::to_string(fd) + ": " + strerror(errno));
    }
  }

  void removeFd(int fd) override {
    if (!isThreadSafe()) {
      post([this, fd]() {
        removeFd(fd);
      });
      return;
    }

    // Tell OS to stop monitoring immediately
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

    if (is_iterating_) {
      pending_removals_.push_back(fd);
      removed_this_batch_.insert(fd);
    } else {
      fd_callbacks_.erase(fd);
    }
  }

  bool isThreadSafe() const override {
    // Empty thread_id implies run() hasn't started yet.
    if (run_thread_id_ == std::thread::id{}) {
      return true; 
    }
    return std::this_thread::get_id() == run_thread_id_;
  }

  const std::string& name() const override { return name_; }

private:
  // --- Infrastructure Setup ---

  static int createEpollInstance() {
    int fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
      throw std::runtime_error("epoll_create1 failed: " + std::string(strerror(errno)));
    }
    return fd;
  }

  static int createWakeupEventFd() {
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
      throw std::runtime_error("eventfd failed: " + std::string(strerror(errno)));
    }
    return fd;
  }

  static void registerWakeupFd(int epoll_fd, int wakeup_fd) {
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wakeup_fd, &ev) < 0) {
      throw std::runtime_error("failed to register wakeup fd into epoll: " + std::string(strerror(errno)));
    }
  }

  // --- Core Event Loop Logic ---

  void processImmediateTasks() {
    std::queue<PostCb> tasks_to_run;
    {
      std::lock_guard<std::mutex> lock(post_mutex_);
      if (post_queue_.empty()) {
        return;
      }
      
      // Swap is O(1) and minimizes the time we hold the mutex lock
      std::swap(tasks_to_run, post_queue_);
    }

    while (!tasks_to_run.empty()) {
      const auto& callback = tasks_to_run.front();
      if (callback) {
        callback();
      }
      tasks_to_run.pop();
    }
  }

  void waitForAndDispatchEvents() {
    is_sleeping_.store(true, std::memory_order_release);

    // Double-check pattern to prevent a data race where a post() happens
    // after processImmediateTasks() but before we go to sleep via epoll_wait.
    if (hasPendingTasks()) {
      is_sleeping_.store(false, std::memory_order_release);
      return; 
    }

    struct epoll_event events[max_events];
    int nfds = epoll_wait(epoll_fd_, events, max_events, epoll_timeout_ms);

    is_sleeping_.store(false, std::memory_order_release);

    if (nfds < 0) {
      handleEpollError();
      return;
    }

    dispatchFiredEvents(events, nfds);
  }

  void handleEpollError() {
    if (errno == EINTR) {
      return; // Harmless interruption by background system signal
    }
    error("[DISPATCHER] epoll_wait error: " + std::string(strerror(errno)));
    exit_requested_ = true; // Initiate graceful failure state
  }

  bool hasPendingTasks() {
    std::lock_guard<std::mutex> lock(post_mutex_);
    return !post_queue_.empty();
  }

  void dispatchFiredEvents(const epoll_event* events, int nfds) {
    // Asserting logic lock. We prevent immediate map mutations during dispatch.
    // If a callback attempts to addFd or removeFd, that action gets deferred.
    is_iterating_ = true;

    for (int i = 0; i < nfds; ++i) {
      int fd = events[i].data.fd;
      uint32_t event_mask = events[i].events;

      // Edge case: A callback executed earlier in this event loop iteration might 
      // have closed or removed an FD that is scheduled to be processed below.
      if (removed_this_batch_.count(fd)) {
        continue;
      }

      handleSingleEvent(fd, event_mask);
    }

    is_iterating_ = false;
  }

  void handleSingleEvent(int fd, uint32_t event_mask) {
    if (fd == wakeup_fd_) {
      drainWakeupEvent();
      // Posted tasks will naturally be processed at the start of the next while-loop cycle.
    } else if (auto it = fd_callbacks_.find(fd); it != fd_callbacks_.end()) {
      it->second(event_mask);
    }
  }

  void applyDeferredMutations() {
    // Safely apply queued changes to our callback maps now that no active iterators exist.
    for (int fd : pending_removals_) {
      fd_callbacks_.erase(fd);
    }
    pending_removals_.clear();
    removed_this_batch_.clear();

    for (auto& [fd, cb] : pending_adds_) {
      fd_callbacks_[fd] = std::move(cb);
    }
    pending_adds_.clear();
  }

  // --- Synchronization & Utilities ---

  void triggerWakeup() const {
    uint64_t val = 1;
    // Suppressing compiler warning. Failing to wakeup via writing implies
    // the max queue threshold is reached which is harmless edge case.
    [[maybe_unused]] ssize_t s = write(wakeup_fd_, &val, sizeof(val));
  }

  void drainWakeupEvent() const {
    uint64_t val;
    // Keep reading until EAGAIN because eventfd is NONBLOCK.
    // Draining the fd guarantees we don't get trapped in a level-triggered endless spin.
    while (read(wakeup_fd_, &val, sizeof(val)) > 0) {}
  }

  // --- State Variables ---

  std::string name_;
  int epoll_fd_{-1};
  int wakeup_fd_{-1};

  std::atomic<bool> running_;
  std::atomic<bool> exit_requested_;
  std::atomic<bool> is_sleeping_{false};
  std::thread::id run_thread_id_;

  std::mutex post_mutex_;
  std::queue<PostCb> post_queue_;

  // Stores lambda handlers for ALL FDs, unifying timers, sockets, etc.
  std::unordered_map<int, FdCb> fd_callbacks_;

  // State flag ensuring we do not invalidate fd_callbacks_ iterators
  bool is_iterating_{false};
  std::vector<int> pending_removals_;
  std::unordered_set<int> removed_this_batch_;
  std::vector<std::pair<int, FdCb>> pending_adds_;
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
