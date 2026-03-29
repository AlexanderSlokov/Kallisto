/**
 * Kallisto Server - Main Entry Point
 * 
 * Envoy-style architecture:
 * - SO_REUSEPORT listeners: each Worker binds and accepts on its own socket
 * - HTTP on port 8200: Vault KV v2 compatible API
 * - Kernel load-balances connections across workers
 */

#include "kallisto/event/worker.hpp"
#include "kallisto/server/http_handler.hpp"
#include "kallisto/server/uds_admin_handler.hpp"
#include "kallisto/kallisto_engine.hpp"
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
    size_t num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 4;
    
    // Parse CLI args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--http-port=") == 0) {
            http_port = static_cast<uint16_t>(std::stoi(arg.substr(12)));
        } else if (arg.find("--workers=") == 0) {
            num_workers = std::stoul(arg.substr(10));
        } else if (arg.find("--db-path=") == 0) {
            // Custom RocksDB path
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: kallisto_server [options]\n"
                      << "  --http-port=PORT   HTTP port (default: 8200)\n"
                      << "  --workers=N        Number of worker threads (default: CPU cores)\n"
                      << "  --db-path=PATH     RocksDB data directory (default: /data/kallisto/rocksdb)\n"
                      << std::endl;
            return 0;
        }
    }
    
    info("========================================");
    info("  Kallisto Secret Server v0.1.0");
    info("  HTTP port:  " + std::to_string(http_port));
    info("  Workers:    " + std::to_string(num_workers));
    info("========================================");
    
    // Setup signal handlers (graceful shutdown)
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::string db_path = "/data/kallisto/rocksdb";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find("--db-path=") == 0) {
            db_path = arg.substr(10);
        }
    }
    
    auto pool = createWorkerPool(num_workers);
    
    auto engine = std::make_shared<KallistoEngine>(db_path);
    info("[SERVER] KallistoEngine created and initialized with DB path: " + db_path);
    
    // Store handlers to prevent premature destruction
    std::vector<std::shared_ptr<server::HttpHandler>> http_handlers;
    
    // Start workers
    pool->start([&]() {
        info("[SERVER] All workers ready, binding listeners...");
        
        for (size_t i = 0; i < pool->size(); ++i) {
            auto& worker = pool->getWorker(i);
            
            // Each worker binds HTTP port (SO_REUSEPORT)
            auto http_handler = std::make_shared<server::HttpHandler>(
                worker.dispatcher(), engine);
            worker.bindListener(http_port, [http_handler](int fd) {
                http_handler->onNewConnection(fd);
            });
            http_handlers.push_back(http_handler);
        }
        
        info("[SERVER] All listeners bound successfully");
    });
    
    // Start Unix Domain Socket Admin Handler
    auto uds_admin = std::make_unique<server::UdsAdminHandler>(engine);
    uds_admin->start();
    
    info("[SERVER] Kallisto is READY. Accepting connections.");
    info("[SERVER] Press Ctrl+C to shutdown.");
    
    // Main loop: wait for shutdown signal
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Graceful shutdown
    info("[SERVER] Initiating graceful shutdown...");
    
    http_handlers.clear();
    
    // Stop worker pool (stops dispatchers, joins threads)
    pool->stop();
    
    // Stop UDS admin
    uds_admin->stop();
    
    // Flush Engine on shutdown
    engine->force_flush();
    info("[SERVER] KallistoEngine flushed.");
    
    info("[SERVER] Shutdown complete.");
    return 0;
}
