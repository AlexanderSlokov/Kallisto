#pragma once

#include "kallisto/event/dispatcher.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/net/listener.hpp"

#include <memory>
#include <cstdint>
#include <string>

// Forward declarations for gRPC types (avoid heavy includes in header)
namespace grpc {
class Server;
class ServerCompletionQueue;
}

namespace kallisto {
namespace server {

/**
 * Async gRPC handler integrated with a Worker's Dispatcher.
 * 
 * Uses timer-based CompletionQueue polling (1ms interval) to bridge
 * gRPC's event model with our epoll-based Dispatcher.
 * 
 * Each Worker gets its own GrpcHandler with its own CompletionQueue.
 * No shared state between handlers — true Envoy-style parallelism.
 * 
 * Future optimization: Replace timer polling with eventfd bridge
 * for sub-microsecond latency (see implementation_plan.md #Future Optimizations).
 */
class GrpcHandler {
public:
    GrpcHandler(event::Dispatcher& dispatcher,
                std::shared_ptr<ShardedCuckooTable> storage);
    ~GrpcHandler();
    
    /**
     * Initialize the gRPC server on the given port.
     * Creates a CompletionQueue and starts timer-based polling.
     * 
     * @param port gRPC port (default: 8201)
     */
    void initialize(uint16_t port);
    
    /**
     * Shutdown the gRPC server gracefully.
     */
    void shutdown();
    
    /**
     * @return true if the handler is initialized and running
     */
    bool isRunning() const { return running_; }
    
    // Base class for async RPC lifecycle (public for TypedCallData inheritance)
    class CallData;

private:
    class SecretServiceImpl;
    
    void pollCompletionQueue();
    void spawnNewCallData();
    void spawnGet();
    void spawnPut();
    void spawnDelete();
    void spawnList();
    
    event::Dispatcher& dispatcher_;
    std::shared_ptr<ShardedCuckooTable> storage_;
    
    std::unique_ptr<SecretServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    event::TimerPtr poll_timer_;
    
    bool running_{false};
};

} // namespace server
} // namespace kallisto
