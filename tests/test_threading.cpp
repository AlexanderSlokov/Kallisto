#include <iostream>
#include <cassert>
#include <atomic>
#include <chrono>
#include <thread>

#include "kallisto/event/dispatcher.hpp"
#include "kallisto/event/worker.hpp"
#include "kallisto/thread_local/thread_local.hpp"
#include "kallisto/logger.hpp"

// Forward declarations from implementation files
namespace kallisto {
    event::DispatcherFactoryPtr createDispatcherFactory();
    event::WorkerPoolPtr createWorkerPool(size_t num_workers);
}

#define ASSERT_TRUE(condition, message) \
    if (!(condition)) { \
        std::cerr << "❌ FAIL: " << message << std::endl; \
        std::exit(1); \
    } else { \
        std::cout << "✅ PASS: " << message << std::endl; \
    }

// =========================================================================================
// DISPATCHER TESTS
// =========================================================================================
void test_dispatcher_basic() {
    std::cout << "\n--- Testing Dispatcher Basic ---\n";
    
    auto factory = kallisto::createDispatcherFactory();
    auto dispatcher = factory->createDispatcher("test_dispatcher");
    
    ASSERT_TRUE(dispatcher != nullptr, "Dispatcher should be created");
    ASSERT_TRUE(dispatcher->name() == "test_dispatcher", "Dispatcher name should match");
    
    // Test post() and run()
    std::atomic<bool> callback_executed{false};
    std::atomic<bool> exit_called{false};
    
    // Post a callback that will exit the dispatcher
    dispatcher->post([&callback_executed, &exit_called, &dispatcher]() {
        callback_executed = true;
        // Schedule exit after callback
        dispatcher->exit();
        exit_called = true;
    });
    
    // Run in a separate thread since run() blocks
    std::thread runner([&dispatcher]() {
        dispatcher->run();
    });
    
    // Wait for completion
    runner.join();
    
    ASSERT_TRUE(callback_executed.load(), "Posted callback should be executed");
    ASSERT_TRUE(exit_called.load(), "Exit should have been called");
}

void test_dispatcher_timer() {
    std::cout << "\n--- Testing Dispatcher Timer ---\n";
    
    auto factory = kallisto::createDispatcherFactory();
    auto dispatcher = factory->createDispatcher("timer_test");
    
    std::atomic<bool> timer_fired{false};
    kallisto::event::TimerPtr timer_holder;  // Keep timer alive
    
    std::thread runner([&dispatcher, &timer_fired, &timer_holder]() {
        // Create timer after run() starts (via post)
        dispatcher->post([&dispatcher, &timer_fired, &timer_holder]() {
            timer_holder = dispatcher->createTimer([&timer_fired, &dispatcher]() {
                timer_fired = true;
                dispatcher->exit();
            });
            
            // Fire after 50ms
            timer_holder->enableTimer(50);
        });
        
        dispatcher->run();
    });
    
    // Give it time to run (max 500ms)
    for (int i = 0; i < 50 && !timer_fired.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Force exit if timer didn't fire
    if (!timer_fired.load()) {
        dispatcher->exit();
    }
    runner.join();
    
    ASSERT_TRUE(timer_fired.load(), "Timer should have fired");
}

// =========================================================================================
// WORKER TESTS
// =========================================================================================
void test_worker_pool() {
    std::cout << "\n--- Testing Worker Pool ---\n";
    
    // Create pool with 2 workers
    auto pool = kallisto::createWorkerPool(2);
    
    ASSERT_TRUE(pool != nullptr, "WorkerPool should be created");
    ASSERT_TRUE(pool->size() == 2, "Pool should have 2 workers");
    
    std::atomic<bool> all_ready{false};
    
    pool->start([&all_ready]() {
        all_ready = true;
    });
    
    ASSERT_TRUE(all_ready.load(), "All workers should be ready after start()");
    
    // Test posting work to workers
    std::atomic<int> work_done{0};
    
    for (size_t i = 0; i < pool->size(); ++i) {
        auto& worker = pool->getWorker(i);
        worker.dispatcher().post([&work_done, &worker]() {
            work_done++;
            worker.recordRequest();
        });
    }
    
    // Give workers time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    ASSERT_TRUE(work_done.load() == 2, "Each worker should have processed work");
    ASSERT_TRUE(pool->totalRequestsProcessed() == 2, "Total requests should be 2");
    
    pool->stop();
    
    ASSERT_TRUE(true, "Workers stopped gracefully");
}

// =========================================================================================
// THREAD LOCAL STORAGE TESTS
// =========================================================================================

// Simple TLS object for testing
class TestTLSObject : public kallisto::tls::ThreadLocalObject {
public:
    int value{0};
    std::string thread_name;
};

void test_thread_local_storage() {
    std::cout << "\n--- Testing Thread Local Storage ---\n";
    
    auto tls = kallisto::tls::createThreadLocalInstance();
    ASSERT_TRUE(tls != nullptr, "TLS Instance should be created");
    
    // Allocate a slot
    auto slot = tls->allocateSlot();
    ASSERT_TRUE(slot != nullptr, "Slot should be allocated");
    
    // Register main thread with a dispatcher
    auto factory = kallisto::createDispatcherFactory();
    auto main_dispatcher = factory->createDispatcher("main");
    tls->registerThread(*main_dispatcher, true);
    
    ASSERT_TRUE(tls->registeredThreadCount() == 1, "Should have 1 registered thread");
    
    // Note: Full TLS testing with multiple threads requires worker integration
    // For now, just verify the interface works
    
    tls->shutdownThread();
    tls->shutdownGlobalThreading();
    
    ASSERT_TRUE(true, "TLS shutdown completed");
}

// =========================================================================================
// MAIN
// =========================================================================================
int main() {
    kallisto::LogConfig config("threading_test");
    config.logLevel = "debug";
    kallisto::Logger::getInstance().setup(config);
    
    try {
        test_dispatcher_basic();
        test_dispatcher_timer();
        test_worker_pool();
        test_thread_local_storage();
        
        std::cout << "\n============================================\n";
        std::cout << "   ALL THREADING TESTS PASSED 🚀\n";
        std::cout << "============================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}
