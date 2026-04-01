/**
 * Kallisto Worker & WorkerPool Tests
 *
 * Adhering to @test-writing standards:
 * - 100% Branch Coverage for `start()`, `stop()`, and deferred state triggers.
 * - Anomaly Testing: Simulate I/O Socket Reservation errors that force
 *   Exception bubble-ups.
 * - Boundary Values: Testing `nullptr` lambda callbacks to ensuring they
 *   do not crash the system.
 */

#include "kallisto/event/worker.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <atomic>

using namespace kallisto;
using namespace kallisto::event;

// Global factory accessor
namespace kallisto {
  event::WorkerPoolPtr createWorkerPool(size_t num_workers);
}

class WorkerTest : public ::testing::Test {
protected:
  void SetUp() override {
    pool_ = createWorkerPool(2);
  }

  void TearDown() override {
    if (pool_) {
      pool_->stop();
    }
  }

  WorkerPoolPtr pool_;
};

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Basic worker lifecycle execution and concurrency thread scale.
// Validates that all condition variables, thread spin-ups, and graceful exits work.
// --------------------------------------------------------------------------------------
TEST_F(WorkerTest, LifecycleStartWaitAndStop) {
  std::atomic<bool> all_ready{false};
  
  // start() is a blocked sync call internally waiting for CV condition.
  // We specify 2 workers, so it will wait until both threads spin up.
  pool_->start([&all_ready]() {
    all_ready = true;
  });

  EXPECT_TRUE(all_ready.load());
  EXPECT_EQ(pool_->size(), 2);
  
  // Boundary Coverage: Stopping works correctly without crashing
  pool_->stop();
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Branch Coverage for duplicate actions and null callbacks.
// start() and stop() logic contains boolean flags ensuring idempotency.
// Callback variables (`on_ready`, `on_all_ready`) could be undefined.
// --------------------------------------------------------------------------------------
TEST_F(WorkerTest, IdempotentBranchesAndNullCallbacks) {
  // Test boundary: pass an empty uninitialized lambda
  pool_->start(nullptr);

  // Grab the direct worker and hit the `if (started_) return;` branch
  Worker& worker = pool_->getWorker(0);
  
  // This should trigger the warning log "[WORKER] already started" but not crash
  worker.start(nullptr);

  // Call pool stop
  pool_->stop();

  // Test branch: call stop() again on the specific worker to hit `if (!started_) return;`
  worker.stop();
  
  // Also call stop() on pool again
  pool_->stop();

  EXPECT_TRUE(true); // Verification completes cleanly if no segfaults occurred.
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: I/O Binding Exception (Graceful Death Simulation)
// Simulating an I/O Port collision by hoarding a port externally without SO_REUSEPORT.
// This tests `handleFailedBinding` fail-fast exception throwing.
// --------------------------------------------------------------------------------------
TEST_F(WorkerTest, IOErrorBindingCollisionThrowsException) {
  pool_->start(nullptr);
  Worker& worker = pool_->getWorker(0);

  // Create hostile socket locally hoarding the port
  int trap_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(trap_fd, 0);
  
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(19912); // Test Port
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  
  int bind_res = bind(trap_fd, (struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(bind_res, 0) << "Hostile setup failed to bind trap socket";

  EXPECT_THROW({
    // Worker uses SO_REUSEPORT, but since our trap socket doesn't, OS will reject it.
    // WorkerImpl::handleFailedBinding must catch -1 and throw runtime_error.
    worker.bindListener(19912, [](int){});
  }, std::runtime_error);

  close(trap_fd);
}

// --------------------------------------------------------------------------------------
// PROBLEM DESCRIPTION: Real Socket Lifecycle & Metric Collection
// Verifies that a full network accept registers onto the dispatcher thread correctly,
// triggers the callback securely, and increments internal atomic diagnostic metrics.
// --------------------------------------------------------------------------------------
TEST_F(WorkerTest, AcceptConnectionAndMetrics) {
  pool_->start(nullptr);
  Worker& worker = pool_->getWorker(1);

  std::atomic<int> accept_count{0};

  // Bind listener to ephemeral-level port securely
  worker.bindListener(19913, [&](int client_fd) {
    accept_count.fetch_add(1);
    close(client_fd);
  });

  // Since listen is active asynchronously, let's create a blocking client socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(19913);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  // Connect to the worker port
  int connect_res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(connect_res, 0);

  // Allow the internal worker dispatcher thread a microsecond to pick up the EPOLLIN event
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  close(sock);

  // Verify internal logging metrics propagated
  EXPECT_EQ(accept_count.load(), 1);
  EXPECT_EQ(worker.requestsProcessed(), 1);
  EXPECT_EQ(pool_->totalRequestsProcessed(), 1);
}
