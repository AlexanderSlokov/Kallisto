/**
 * Unit test for SO_REUSEPORT Listener.
 * 
 * Verifies:
 * - Multiple threads can bind the same port with SO_REUSEPORT
 * - Non-blocking accept returns EAGAIN when no connections
 * - Connections are accepted successfully
 * - Sockets are properly cleaned up
 */

#include "kallisto/net/listener.hpp"
#include "kallisto/logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <chrono>

// Simple assertion macro
#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        error("[FAIL] " + std::string(msg) + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        return false; \
    } \
} while(0)

using namespace kallisto;

// ---------------------------------------------------------------------------
// Test: SO_REUSEPORT allows multiple sockets on same port
// ---------------------------------------------------------------------------
bool testMultipleBindSamePort() {
    info("[TEST] testMultipleBindSamePort");
    
    const uint16_t port = 19900;
    const int num_sockets = 4;
    std::vector<int> fds;
    
    for (int i = 0; i < num_sockets; i++) {
        int fd = net::Listener::createListenSocket(port, true);
        ASSERT_TRUE(fd >= 0, "Failed to create listen socket #" + std::to_string(i));
        fds.push_back(fd);
    }
    
    info("[TEST] Successfully bound " + std::to_string(num_sockets) + 
         " sockets to port " + std::to_string(port));
    
    // Cleanup
    for (int fd : fds) {
        net::Listener::closeSocket(fd);
    }
    
    info("[PASS] testMultipleBindSamePort");
    return true;
}

// ---------------------------------------------------------------------------
// Test: Non-blocking accept returns -1 when no connections pending
// ---------------------------------------------------------------------------
bool testNonBlockingAcceptEagain() {
    info("[TEST] testNonBlockingAcceptEagain");
    
    const uint16_t port = 19901;
    int listen_fd = net::Listener::createListenSocket(port, true);
    ASSERT_TRUE(listen_fd >= 0, "Failed to create listen socket");
    
    // Accept with no pending connections should return -1 (EAGAIN)
    int client_fd = net::Listener::acceptConnection(listen_fd, nullptr);
    ASSERT_TRUE(client_fd < 0, "Accept should return -1 when no connections pending");
    
    net::Listener::closeSocket(listen_fd);
    
    info("[PASS] testNonBlockingAcceptEagain");
    return true;
}

// ---------------------------------------------------------------------------
// Test: Accept a connection successfully
// ---------------------------------------------------------------------------
bool testAcceptConnection() {
    info("[TEST] testAcceptConnection");
    
    const uint16_t port = 19902;
    int listen_fd = net::Listener::createListenSocket(port, true);
    ASSERT_TRUE(listen_fd >= 0, "Failed to create listen socket");
    
    // Create a client connection in background
    std::atomic<bool> connected{false};
    std::thread client_thread([port, &connected]() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        connected.store(ret == 0);
        
        // Keep connection open briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        close(sock);
    });
    
    // Wait for client to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Accept the connection
    struct sockaddr_in client_addr{};
    int client_fd = net::Listener::acceptConnection(listen_fd, &client_addr);
    ASSERT_TRUE(client_fd >= 0, "Failed to accept connection");
    ASSERT_TRUE(connected.load(), "Client should have connected");
    
    info("[TEST] Accepted client fd=" + std::to_string(client_fd));
    
    close(client_fd);
    client_thread.join();
    net::Listener::closeSocket(listen_fd);
    
    info("[PASS] testAcceptConnection");
    return true;
}

// ---------------------------------------------------------------------------
// Test: Multiple workers accept connections on same port
// ---------------------------------------------------------------------------
bool testMultiWorkerAccept() {
    info("[TEST] testMultiWorkerAccept");
    
    const uint16_t port = 19903;
    const int num_workers = 4;
    const int num_connections = 20;
    
    // Create listen sockets (one per "worker")
    std::vector<int> listen_fds;
    for (int i = 0; i < num_workers; i++) {
        int fd = net::Listener::createListenSocket(port, true);
        ASSERT_TRUE(fd >= 0, "Worker " + std::to_string(i) + " bind failed");
        listen_fds.push_back(fd);
    }
    
    // Count accepted connections per worker
    std::vector<std::atomic<int>> accept_counts(num_workers);
    for (auto& c : accept_counts) {
        c.store(0);
    }
    
    std::atomic<bool> stop_workers{false};
    
    // Start worker threads
    std::vector<std::thread> workers;
    workers.reserve(num_workers);
for (int i = 0; i < num_workers; i++) {
        workers.emplace_back([&, i]() {
            while (!stop_workers.load()) {
                int client_fd = net::Listener::acceptConnection(listen_fds[i], nullptr);
                if (client_fd >= 0) {
                    accept_counts[i].fetch_add(1);
                    close(client_fd);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // Create client connections
    for (int i = 0; i < num_connections; i++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        
        connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        close(sock);
    }
    
    // Wait for acceptance
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop_workers.store(true);
    
    for (auto& t : workers) { 
		t.join();
	}
    
    // Count total accepted
    int total = 0;
    for (int i = 0; i < num_workers; i++) {
        int count = accept_counts[i].load();
        info("[TEST] Worker " + std::to_string(i) + " accepted " + 
             std::to_string(count) + " connections");
        total += count;
    }
    
    info("[TEST] Total accepted: " + std::to_string(total) + 
         " / " + std::to_string(num_connections));
    
    ASSERT_TRUE(total > 0, "At least some connections should be accepted");
    
    // Cleanup
    for (int fd : listen_fds) {
        net::Listener::closeSocket(fd);
    }
    
    info("[PASS] testMultiWorkerAccept");
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    info("=== Kallisto SO_REUSEPORT Listener Tests ===");
    
    int passed = 0;
    int failed = 0;
    
    auto run = [&](bool (*test)(), const char* name) {
        if (test()) {
            passed++;
        } else {
            error("[FAIL] " + std::string(name));
            failed++;
        }
    };
    
    run(testMultipleBindSamePort, "testMultipleBindSamePort");
    run(testNonBlockingAcceptEagain, "testNonBlockingAcceptEagain");
    run(testAcceptConnection, "testAcceptConnection");
    run(testMultiWorkerAccept, "testMultiWorkerAccept");
    
    info("=== Results: " + std::to_string(passed) + " passed, " + 
         std::to_string(failed) + " failed ===");
    
    return failed > 0 ? 1 : 0;
}
