/**
 * Kallisto DispatcherImpl Tests
 *
 * Adhering to @test-writing standards:
 * - Problem Descriptions: clearly defined per suite
 * - Anomaly Testing: Exception bubbling and structural limits (EBADF invalid mutations)
 * - 100% Branch Coverage targets (Deferring logic on maps, Timer triggers)
 */

#include "kallisto/event/dispatcher.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <new>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>

using namespace kallisto;
using namespace kallisto::event;

namespace kallisto {
  // Externally defined in dispatcher_impl.cpp
  event::DispatcherFactoryPtr createDispatcherFactory();
}

class DispatcherTest : public ::testing::Test {
protected:
  void SetUp() override {
    factory_ = createDispatcherFactory();
    dispatcher_ = factory_->createDispatcher("test_worker");
  }

  void TearDown() override {
    // Ensure the event loop fully dies before teardown by invoking exit naturally 
    // in case a test leaves it running.
    dispatcher_->exit();
    dispatcher_.reset();
    factory_.reset();
  }

  DispatcherFactoryPtr factory_;
  DispatcherPtr dispatcher_;
};

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Standard Post & Exit
// Verifies that post() queues items across threads securely and that exit() reliably
// interrupts the blocking epoll loop. 1000 posts ensures pipeline scale stress.
// --------------------------------------------------------------------------------------
TEST_F(DispatcherTest, PostCrossThreadAndExit) {
  std::atomic<int> counter{0};
  
  std::thread t([&]() {
    dispatcher_->run();
  });

  // Post 1000 tasks
  for (int i = 0; i < 1000; i++) {
    dispatcher_->post([&counter]() { counter++; });
  }

  // A final task to trigger exit
  dispatcher_->post([&]() { 
    dispatcher_->exit(); 
  });

  t.join();
  EXPECT_EQ(counter.load(), 1000);
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Timer execution and cancellation
// Verifies that a physical Linux timerfd operates correctly on the epoll event loop,
// firing at the correct time, and that disableTimer prevents execution.
// --------------------------------------------------------------------------------------
TEST_F(DispatcherTest, TimerExecutionAndCancellation) {
  std::atomic<int> timer_fired_count{0};
  
  auto timer = dispatcher_->createTimer([&]() {
    timer_fired_count++;
    dispatcher_->exit();
  });

  timer->enableTimer(10); // 10ms

  std::thread t([&]() {
    dispatcher_->run();
  });

  t.join();
  EXPECT_EQ(timer_fired_count.load(), 1);

  // Restart testing cancellation
  timer_fired_count = 0;
  auto canceled_timer = dispatcher_->createTimer([&]() {
    timer_fired_count++;
  });
  
  canceled_timer->enableTimer(10);
  canceled_timer->disableTimer();

  auto restart_timer = dispatcher_->createTimer([&]() {
    dispatcher_->exit();
  });
  restart_timer->enableTimer(50); // Give the canceled timer 50ms to misfire

  std::thread t2([&]() {
    dispatcher_->run();
  });

  t2.join();
  // Ensure the canceled one never fired
  EXPECT_EQ(timer_fired_count.load(), 0);
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Deferred map mutations during iterator safety trigger (MC/DC)
// Dispatcher must block modifications to fd_callbacks_ map during dispatchFiredEvents
// to avoid iterator invalidation segmentation faults. It must defer them to a pending queue.
// We trigger this by registering an FD, firing it, and having its callback remove it.
// --------------------------------------------------------------------------------------
TEST_F(DispatcherTest, DeferredMutationsDuringDispatch) {
  int sv[2]; // UNIX Socketpair
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

  bool was_fired = false;

  dispatcher_->addFd(sv[1], EPOLLIN, [&](uint32_t) {
    was_fired = true;
    
    // Boundary anomaly logic: removeFd called inside iterating cycle
    dispatcher_->removeFd(sv[1]);
    
    // We add another FD while iterating to check pending_adds_ handling queue
    dispatcher_->addFd(sv[0], EPOLLIN, [](uint32_t){});
    
    dispatcher_->exit();
  });

  // Trigger it
  char byte = 'X';
  ASSERT_EQ(write(sv[0], &byte, 1), 1) << "Failed to write trigger byte to socketpair";

  std::thread t([&]() {
    dispatcher_->run();
  });

  t.join();
  
  EXPECT_TRUE(was_fired);

  close(sv[0]);
  close(sv[1]);
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: OOM / Exception bubble up (Graceful Death Simulation)
// If a posted callback throws std::bad_alloc or runtime_error, the dispatcher must NOT
// swallow it invisibly. It should bubble up to the caller of run() so the worker thread
// can die gracefully rather than silently halting inside an infinite loop.
// --------------------------------------------------------------------------------------
TEST_F(DispatcherTest, ExceptionInCallbackBubblesUpToRunCaller) {
  dispatcher_->post([]() {
    throw std::bad_alloc();
  });

  EXPECT_THROW({
    dispatcher_->run();
  }, std::bad_alloc);
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: I/O Fault Tolerance (Negative FD manipulation)
// Simulates an error case where external systems attempt to bind or modify invalid FD.
// Dispatcher must log internally but absolutely never crash or throw during runtime loop.
// --------------------------------------------------------------------------------------
TEST_F(DispatcherTest, ModifyInvalidFdDoesNotCrash) {
  // -1 is definitely an invalid FD. System logs an error but handles gracefully
  dispatcher_->modifyFd(-1, EPOLLIN);
  
  dispatcher_->addFd(-2, EPOLLIN, [](uint32_t){});

  EXPECT_TRUE(true); // Verifies execution proceeds seamlessly
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Thread Safety Validation
// Dispatcher must correctly detect when it's execution thread is matching
// --------------------------------------------------------------------------------------
TEST_F(DispatcherTest, IsThreadSafeFlag) {
  // Before running, thread safety should default TRUE
  EXPECT_TRUE(dispatcher_->isThreadSafe());
  
  std::atomic<bool> inside_thread_safety{false};
  
  dispatcher_->post([&]() {
    inside_thread_safety = dispatcher_->isThreadSafe();
    dispatcher_->exit();
  });
  
  std::thread t([&]() {
    dispatcher_->run();
  });
  
  t.join();
  
  EXPECT_TRUE(inside_thread_safety.load());
}
