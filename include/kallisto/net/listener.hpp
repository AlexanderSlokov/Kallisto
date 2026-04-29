#pragma once

#include <cstdint>
#include <netinet/in.h>

namespace kallisto {
namespace net {

/**
 * Socket utilities for SO_REUSEPORT listener pattern.
 * 
 * Each Worker creates its own listening socket on the same port.
 * Kernel distributes incoming connections across all workers via SO_REUSEPORT.
 * This eliminates the "Request Router" bottleneck entirely.
 * 
 * Pattern (Envoy-style):
 *   Worker 0: bind(:8200, SO_REUSEPORT) -> epoll -> accept() -> handle
 *   Worker 1: bind(:8200, SO_REUSEPORT) -> epoll -> accept() -> handle
 *   Worker n: bind(:8200, SO_REUSEPORT) -> epoll -> accept() -> handle
 * 
 * Requires Linux 3.9+ for SO_REUSEPORT.
 */
class Listener {
public:
    /**
     * Create a non-blocking listening socket with SO_REUSEPORT.
     * Multiple processes/threads can bind to the same port.
     * 
     * @param port Port number to listen on
     * @param reuseport Enable SO_REUSEPORT (default: true)
     * @return File descriptor of the listening socket, or -1 on error
     */
    static int createListenSocket(uint16_t port, bool reuseport = true);
    
    /**
     * Accept a connection (non-blocking).
     * Returns immediately with -1 and errno=EAGAIN if no pending connections.
     * 
     * @param listen_fd Listening socket file descriptor
     * @param addr Output: client address (can be nullptr)
     * @return Client socket fd, or -1 if no connection pending
     */
    static int acceptConnection(int listen_fd, struct sockaddr_in* addr);
    
    /**
     * Set socket to non-blocking mode.
     */
    static void setNonBlocking(int fd);
    
    /**
     * Set TCP_NODELAY (disable Nagle's algorithm) for low-latency responses.
     */
    static void setTcpNoDelay(int fd);
    
    /**
     * Close a socket fd safely.
     */
    static void closeSocket(int fd);
};

} // namespace net
} // namespace kallisto
