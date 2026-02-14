#pragma once

#include "kallisto/event/dispatcher.hpp"
#include "kallisto/sharded_cuckoo_table.hpp"
#include "kallisto/net/listener.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>

namespace kallisto {
namespace server {

/**
 * Minimal HTTP/1.1 handler for Vault KV v2 API compatibility.
 * 
 * STRICT SCOPE (Quorum Review):
 * - Only supports Content-Length requests
 * - Rejects chunked encoding and Expect: 100-continue with 400
 * - Header parsing via string splitting, body parsing via simdjson
 * 
 * Vault API Routes:
 *   GET    /v1/secret/data/:path  -> lookup
 *   POST   /v1/secret/data/:path  -> insert
 *   DELETE /v1/secret/data/:path  -> remove
 * 
 * Each Worker has its own HttpHandler — no shared state.
 */
class HttpHandler {
public:
    HttpHandler(event::Dispatcher& dispatcher,
                std::shared_ptr<ShardedCuckooTable> storage);
    ~HttpHandler();
    
    /**
     * Handle a newly accepted client connection.
     * Registers the fd with this worker's epoll for reading.
     * 
     * @param client_fd Non-blocking client socket fd
     */
    void onNewConnection(int client_fd);
    
    /**
     * @return Number of active connections on this handler
     */
    size_t activeConnections() const { return connections_.size(); }

private:
    // Per-connection state
    struct Connection {
        int fd;
        std::string read_buffer;
        std::string write_buffer;
        size_t write_offset{0};
        bool keep_alive{false};
    };
    
    // HTTP request (parsed)
    struct HttpRequest {
        std::string method;
        std::string path;
        std::string body;
        int content_length{0};
        bool keep_alive{true};
        bool valid{false};
    };
    
    void onReadable(int fd);
    void onWritable(int fd);
    void closeConnection(int fd);
    
    // Parse HTTP request from buffer
    HttpRequest parseRequest(const std::string& buffer);
    
    // Route request to handler
    void handleRequest(Connection& conn, const HttpRequest& req);
    
    // Vault API handlers
    void handleGetSecret(Connection& conn, const std::string& path);
    void handlePutSecret(Connection& conn, const std::string& path, 
                         const std::string& body);
    void handleDeleteSecret(Connection& conn, const std::string& path);
    
    // HTTP response helpers
    void sendResponse(Connection& conn, int status_code, 
                      const std::string& content_type, const std::string& body);
    void sendError(Connection& conn, int status_code, const std::string& message);
    static std::string statusText(int code);
    
    event::Dispatcher& dispatcher_;
    std::shared_ptr<ShardedCuckooTable> storage_;
    std::unordered_map<int, Connection> connections_;
};

} // namespace server
} // namespace kallisto
