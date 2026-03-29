#include "kallisto/net/listener.hpp"
#include "kallisto/logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

namespace kallisto {
namespace net {

int Listener::createListenSocket(uint16_t port, bool reuseport) {
    // Create socket: non-blocking + close-on-exec
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error("[LISTENER] Failed to create socket: " + std::string(strerror(errno)));
        return -1;
    }
    
    int enable = 1;
    
    // SO_REUSEADDR: allow rebinding immediately after restart
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        error("[LISTENER] setsockopt SO_REUSEADDR failed: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }
    
    // SO_REUSEPORT: allow multiple sockets to bind same port
    // Kernel load-balances incoming connections across all bound sockets
    if (reuseport) {
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
            error("[LISTENER] setsockopt SO_REUSEPORT failed: " + std::string(strerror(errno)));
            close(fd);
            return -1;
        }
    }
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        error("[LISTENER] bind(::" + std::to_string(port) + ") failed: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }
    
    if (listen(fd, SOMAXCONN) < 0) {
        error("[LISTENER] listen failed: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }
    
    info("[LISTENER] Listening on port " + std::to_string(port) + 
         (reuseport ? " (SO_REUSEPORT)" : ""));
    
    return fd;
}

int Listener::acceptConnection(int listen_fd, struct sockaddr_in* addr) {
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    // Non-blocking accept with SOCK_NONBLOCK | SOCK_CLOEXEC on the new fd
    int client_fd = accept4(listen_fd, 
                            reinterpret_cast<struct sockaddr*>(&client_addr), 
                            &addr_len,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
    
    if (client_fd < 0) {
        // EAGAIN/EWOULDBLOCK = no pending connections (normal for non-blocking)
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            error("[LISTENER] accept failed: " + std::string(strerror(errno)));
        }
        return -1;
    }
    
    // Set TCP_NODELAY for low-latency response
    setTcpNoDelay(client_fd);
    
    if (addr) {
        *addr = client_addr;
    }
    
    return client_fd;
}

void Listener::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void Listener::setTcpNoDelay(int fd) {
    int enable = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
}

void Listener::closeSocket(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

} // namespace net
} // namespace kallisto
