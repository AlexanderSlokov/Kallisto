#include "kallisto/event/worker.hpp"
#include "kallisto/logger.hpp"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace kallisto {

// Forward declaration from dispatcher_impl.cpp
event::DispatcherFactoryPtr createDispatcherFactory();

namespace event {

/**
 * Worker implementation.
 */
class WorkerImpl : public Worker {
public:
    WorkerImpl(uint32_t index, const std::string& name, DispatcherPtr dispatcher)
        : index_(index)
        , name_(name)
        , dispatcher_(std::move(dispatcher))
        , requests_processed_(0)
        , started_(false) {
    }

    ~WorkerImpl() override {
        if (thread_.joinable()) {
            stop();
        }
    }

    void start(std::function<void()> on_ready) override {
        if (started_) {
            warn("[WORKER] Worker " + name_ + " already started");
            return;
        }

        started_ = true;
        on_ready_ = std::move(on_ready);

        thread_ = std::thread([this]() {
            threadRoutine();
        });
    }

    void stop() override {
        if (!started_) return;

        debug("[WORKER] Stopping worker " + name_);
        
        // Signal dispatcher to exit
        dispatcher_->exit();
        
        // Wait for thread to finish
        if (thread_.joinable()) {
            thread_.join();
        }
        
        started_ = false;
        debug("[WORKER] Worker " + name_ + " stopped");
    }

    Dispatcher& dispatcher() override {
        return *dispatcher_;
    }

    uint32_t index() const override {
        return index_;
    }

    uint64_t requestsProcessed() const override {
        return requests_processed_.load(std::memory_order_relaxed);
    }

    void recordRequest() override {
        requests_processed_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    void threadRoutine() {
        info("[WORKER] Worker " + name_ + " thread started");

        // Post the on_ready callback to run on dispatcher thread
        // This ensures it runs after the event loop starts
        dispatcher_->post([this]() {
            if (on_ready_) {
                on_ready_();
            }
        });

        // Run the event loop (blocks until exit() is called)
        dispatcher_->run();

        info("[WORKER] Worker " + name_ + " thread exiting");
    }

    uint32_t index_;
    std::string name_;
    DispatcherPtr dispatcher_;
    std::atomic<uint64_t> requests_processed_;
    std::atomic<bool> started_;
    std::function<void()> on_ready_;
    std::thread thread_;
};

/**
 * WorkerFactory implementation.
 */
class WorkerFactoryImpl : public WorkerFactory {
public:
    WorkerFactoryImpl() : dispatcher_factory_(createDispatcherFactory()) {}

    WorkerPtr createWorker(uint32_t index, const std::string& name_prefix) override {
        std::string name = name_prefix + ":" + std::to_string(index);
        auto dispatcher = dispatcher_factory_->createDispatcher(name);
        return std::make_unique<WorkerImpl>(index, name, std::move(dispatcher));
    }

private:
    DispatcherFactoryPtr dispatcher_factory_;
};

/**
 * WorkerPool implementation.
 */
class WorkerPoolImpl : public WorkerPool {
public:
    explicit WorkerPoolImpl(size_t num_workers) {
        if (num_workers == 0) {
            num_workers = std::thread::hardware_concurrency();
            if (num_workers == 0) num_workers = 4;  // Fallback
        }
        
        info("[WORKER_POOL] Creating " + std::to_string(num_workers) + " workers");
        
        auto factory = std::make_unique<WorkerFactoryImpl>();
        workers_.reserve(num_workers);
        
        for (size_t i = 0; i < num_workers; ++i) {
            workers_.push_back(factory->createWorker(i, "wrk"));
        }
    }

    void start(std::function<void()> on_all_ready) override {
        std::atomic<size_t> ready_count{0};
        std::mutex mtx;
        std::condition_variable cv;
        
        size_t total = workers_.size();
        
        for (auto& worker : workers_) {
            worker->start([&ready_count, &mtx, &cv, total, on_all_ready]() {
                size_t count = ready_count.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (count == total) {
                    // Last worker ready, notify
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                    }
                    cv.notify_one();
                }
            });
        }
        
        // Wait for all workers to be ready
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&ready_count, total]() {
                return ready_count.load(std::memory_order_acquire) == total;
            });
        }
        
        info("[WORKER_POOL] All " + std::to_string(total) + " workers ready");
        
        if (on_all_ready) {
            on_all_ready();
        }
    }

    void stop() override {
        debug("[WORKER_POOL] Stopping all workers");
        for (auto& worker : workers_) {
            worker->stop();
        }
        debug("[WORKER_POOL] All workers stopped");
    }

    size_t size() const override {
        return workers_.size();
    }

    Worker& getWorker(size_t index) override {
        return *workers_[index];
    }

    uint64_t totalRequestsProcessed() const override {
        uint64_t total = 0;
        for (const auto& worker : workers_) {
            total += worker->requestsProcessed();
        }
        return total;
    }

private:
    std::vector<WorkerPtr> workers_;
};

} // namespace event

// Factory function for WorkerPool
event::WorkerPoolPtr createWorkerPool(size_t num_workers) {
    return std::make_unique<event::WorkerPoolImpl>(num_workers);
}

} // namespace kallisto
