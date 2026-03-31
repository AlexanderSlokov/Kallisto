/**
 * Kallisto ThreadLocal Storage Tests
 */

#include "kallisto/thread_local/thread_local.hpp"
#include "kallisto/event/dispatcher.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <stdexcept>
#include <memory>

using namespace kallisto;
using namespace kallisto::thread_local_storage;

// Mock Dispatcher for testing without creating real network sockets
class MockDispatcher : public event::Dispatcher {
public:
  MockDispatcher(const std::string& name) : name_(name) {}
  
  void run() override {}
  void exit() override {}
  
  void post(event::Dispatcher::PostCb cb) override {
    // In our tests, just run the callback synchronously to simulate dispatcher execution 
    // for predictable TLS validation.
    cb();
  }
  
  event::TimerPtr createTimer(std::function<void()> /*cb*/) override { return nullptr; }
  void addFd(int /*fd*/, uint32_t /*events*/, event::Dispatcher::FdCb /*cb*/) override {}
  void modifyFd(int /*fd*/, uint32_t /*events*/) override {}
  void removeFd(int /*fd*/) override {}
  
  bool isThreadSafe() const override { return true; }
  const std::string& name() const override { return name_; }

private:
  std::string name_;
};

class SimpleTestObj : public ThreadLocalObject {
public:
  SimpleTestObj(int v) : val(v) {}
  int val;
};

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Standard Usage & Boundary Values
// We need to ensure that unregistered threads correctly return null, that initialized 
// threads retrieve correct data, and bounded checks restrict out-of-bounds slot usage.
// --------------------------------------------------------------------------------------
TEST(ThreadLocalTest, RegistrationAndBasicBoundaryChecks) {
  auto tls = createThreadLocalInstance();
  auto slot = tls->allocateSlot();
  
  // Boundary Value: Unregistered thread calling get()
  EXPECT_EQ(slot->get(), nullptr);
  
  MockDispatcher dispatcher("Main");
  tls->registerThread(dispatcher, true);
  
  EXPECT_EQ(tls->registeredThreadCount(), 1);
  
  // Boundary Value: Slot without value yet in registered thread 
  // (index out of bounds internally because `set` hasn't expanded tls_data.slots)
  EXPECT_EQ(slot->get(), nullptr);
  
  // Initialize
  slot->set([](event::Dispatcher&) -> ThreadLocalObjectPtr {
    return std::make_shared<SimpleTestObj>(42);
  });
  
  auto obj = std::dynamic_pointer_cast<SimpleTestObj>(slot->get());
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->val, 42);
  
  // Branch Coverage: Shutdown cleans up correctly
  tls->shutdownThread();
  EXPECT_EQ(slot->get(), nullptr);
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Double Registration Anomaly (Graceful Death / Exception)
// If a dispatcher attempts to register twice, the TLS system should detect this state
// deviation and fail fast by throwing an exception, rather than crashing or swallowing.
// --------------------------------------------------------------------------------------
TEST(ThreadLocalTest, DoubleRegistrationThrowsException) {
  auto tls = createThreadLocalInstance();
  MockDispatcher dispatcher("Worker");
  
  tls->registerThread(dispatcher, false);
  
  EXPECT_THROW({
    tls->registerThread(dispatcher, false);
  }, std::logic_error);
  
  tls->shutdownThread();
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Exception Safety in Initializer (OOM Simulation)
// If a Slot::set() triggers an out-of-memory exception or similar fault during 
// the initializer lambda, the system must bubble it up safely.
// --------------------------------------------------------------------------------------
TEST(ThreadLocalTest, InitializerThrowsOOMException) {
  auto tls = createThreadLocalInstance();
  MockDispatcher dispatcher("Worker");
  tls->registerThread(dispatcher, false);
  
  auto slot = tls->allocateSlot();
  
  EXPECT_THROW({
    slot->set([](event::Dispatcher&) -> ThreadLocalObjectPtr {
      throw std::bad_alloc(); // Simulate malloc failure / OOM
    });
  }, std::bad_alloc);
  
  tls->shutdownThread();
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Global Shutdown Safety
// Anomaly test ensuring that when global shutdown is invoked, subsequent actions 
// do not result in segmentation faults, effectively freeing raw slots reliably.
// --------------------------------------------------------------------------------------
TEST(ThreadLocalTest, GlobalShutdownSafety) {
  auto tls = createThreadLocalInstance();
  MockDispatcher dispatcher("Worker");
  tls->registerThread(dispatcher, false);
  
  auto slot1 = tls->allocateSlot();
  auto slot2 = tls->allocateSlot();
  
  tls->shutdownGlobalThreading(); // Invoked manually
  
  // Idempotence check (Branch Coverage)
  tls->shutdownGlobalThreading(); 
  
  // Even if thread local data still references old slots, tests ensure system doesn't crash
  tls->shutdownThread();
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Post-Registration Initialization Synchronization
// MC/DC testing the scenario where a slot is initialized *after* a thread registers. 
// It verifies the deferred callback mechanism posts properly to active dispatchers.
// --------------------------------------------------------------------------------------
TEST(ThreadLocalTest, SlotSetAfterRegistration) {
  auto tls = createThreadLocalInstance();
  MockDispatcher dispatcher1("Worker1");
  MockDispatcher dispatcher2("Worker2");
  
  tls->registerThread(dispatcher1, false);
  // We simulate threads sequentially since MockDispatcher's post is synchronous
  // It handles exactly what we want without complex real threading here.
  
  auto slot = tls->allocateSlot();
  
  int call_count = 0;
  slot->set([&call_count](event::Dispatcher&) -> ThreadLocalObjectPtr {
    call_count++;
    return std::make_shared<SimpleTestObj>(100);
  });
  
  // The first dispatcher had `post` executed synchronously.
  EXPECT_EQ(call_count, 1);
  
  // Because TLS is single-threaded structurally but simulate-able, we reset thread context manually 
  // to avoid cross-test pollution since TLS is static thread_local.
  tls->shutdownThread();

  // Now we register the second thread. It should trigger initialization immediately inside `registerThread`.
  tls->registerThread(dispatcher2, false);
  
  EXPECT_EQ(call_count, 2);
  
  tls->shutdownThread();
}
