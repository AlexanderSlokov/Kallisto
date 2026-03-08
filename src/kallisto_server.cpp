/**
 * Kallisto Server - Main Entry Point
 * 
 * Envoy-style architecture:
 * - SO_REUSEPORT listeners: each Worker binds and accepts on its own socket
 * - gRPC on port 8201: async CompletionQueue per worker
 * - HTTP on port 8200: Vault KV v2 compatible API
 * - Kernel load-balances connections across workers
 */

#include "kallisto/event/worker.hpp"
#include "kallisto/server/grpc_handler.hpp"
#include "kallisto/server/http_handler.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/rocksdb_storage.hpp"
#include "kallisto/btree_index.hpp"
#include "kallisto/logger.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

namespace {
    std::atomic<bool> shutdown_requested{false};
    
    void signalHandler(int signum) {
        kallisto::info("[SERVER] Received signal " + std::to_string(signum) + ", shutting down...");
        shutdown_requested.store(true);
    }
}

// Factory from worker_impl.cpp
namespace kallisto {
    event::WorkerPoolPtr createWorkerPool(size_t num_workers);
}

int main(int argc, char** argv) {
    using namespace kallisto;
    
    // Ignore SIGPIPE — clients may disconnect during send()
    signal(SIGPIPE, SIG_IGN);
    
    // Default to WARN to avoid stdout bottleneck in benchmarks
    Logger::getInstance().setLevel(LogLevel::WARN);
    
    // Default configuration
    uint16_t http_port = 8200;
    uint16_t grpc_port = 8201;
    size_t num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 4;
    
    // Parse CLI args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--http-port=") == 0) {
            http_port = static_cast<uint16_t>(std::stoi(arg.substr(12)));
        } else if (arg.find("--grpc-port=") == 0) {
            grpc_port = static_cast<uint16_t>(std::stoi(arg.substr(12)));
        } else if (arg.find("--workers=") == 0) {
            num_workers = std::stoul(arg.substr(10));
        } else if (arg.find("--db-path=") == 0) {
            // Custom RocksDB path
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kallisto_server [options]\n"
                      << "  --http-port=PORT   HTTP port (default: 8200)\n"
                      << "  --grpc-port=PORT   gRPC port (default: 8201)\n"
                      << "  --workers=N        Number of worker threads (default: CPU cores)\n"
                      << "  --db-path=PATH     RocksDB data directory (default: /data/kallisto/rocksdb)\n"
                      << std::endl;
            return 0;
        }
    }
    
    info("========================================");
    info("  Kallisto Secret Server v0.1.0");
    info("  HTTP port:  " + std::to_string(http_port));
    info("  gRPC port:  " + std::to_string(grpc_port));
    info("  Workers:    " + std::to_string(num_workers));
    info("========================================");
    
    // Setup signal handlers (graceful shutdown)
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Create shared storage (1M bucket capacity)
    auto storage = std::make_shared<ShardedCuckooTable>(1024 * 1024);
    info("[SERVER] ShardedCuckooTable created (1M buckets, 64 shards)");
    
    // Create B-Tree firewall (O(logN) gateway)
    auto path_index = std::make_shared<BTreeIndex>(5);
    info("[SERVER] BTreeIndex created");

    // Create RocksDB persistence layer
    // Parse --db-path if provided
    std::string db_path = "/data/kallisto/rocksdb";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--db-path=") == 0) {
            db_path = arg.substr(10);
        }
    }
    auto persistence = std::make_shared<RocksDBStorage>(db_path);
    if (persistence->is_open()) {
        info("[SERVER] RocksDB persistence layer opened at: " + db_path);
        
        // Populate B-Tree from RocksDB so cache-miss aren't blocked
        size_t count = 0;
        persistence->iterate_all([&](const SecretEntry& entry) {
            path_index->insert_path(entry.path);
            count++;
        });
        info("[SERVER] Rebuilt B-Tree index with " + std::to_string(count) + " paths");
    } else {
        warn("[SERVER] RocksDB persistence unavailable — running in-memory only");
        persistence = nullptr;  // Handlers check for nullptr
    }
    
    // Create worker pool
    auto pool = createWorkerPool(num_workers);
    
    // Store handlers to prevent premature destruction
    std::vector<std::shared_ptr<server::HttpHandler>> http_handlers;
    std::vector<std::shared_ptr<server::GrpcHandler>> grpc_handlers;
    
    // Start workers
    pool->start([&]() {
        info("[SERVER] All workers ready, binding listeners...");
        
        for (size_t i = 0; i < pool->size(); ++i) {
            auto& worker = pool->getWorker(i);
            
            // Each worker binds HTTP port (SO_REUSEPORT)
            auto http_handler = std::make_shared<server::HttpHandler>(
                worker.dispatcher(), storage, persistence, path_index);
            worker.bindListener(http_port, [http_handler](int fd) {
                http_handler->onNewConnection(fd);
            });
            http_handlers.push_back(http_handler);
            
            // Each worker binds gRPC port (SO_REUSEPORT)
            // Note: gRPC manages its own listening socket internally,
            //       but we use SO_REUSEPORT address for the builder
            auto grpc_handler = std::make_shared<server::GrpcHandler>(
                worker.dispatcher(), storage, persistence, path_index);
            grpc_handler->initialize(grpc_port);
            grpc_handlers.push_back(grpc_handler);
        }
        
        info("[SERVER] All listeners bound successfully");
    });
    
    info("[SERVER] Kallisto is READY. Accepting connections.");
    info("[SERVER] Press Ctrl+C to shutdown.");
    
    // Main loop: wait for shutdown signal
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Graceful shutdown
    info("[SERVER] Initiating graceful shutdown...");
    
    // Shutdown gRPC handlers first (they have their own timers)
    for (auto& handler : grpc_handlers) {
        handler->shutdown();
    }
    grpc_handlers.clear();
    http_handlers.clear();
    
    // Stop worker pool (stops dispatchers, joins threads)
    pool->stop();
    
    // Flush RocksDB on shutdown to ensure all data is persisted
    if (persistence) {
        persistence->flush();
        info("[SERVER] RocksDB flushed.");
    }
    
    info("[SERVER] Shutdown complete.");
    return 0;
}
