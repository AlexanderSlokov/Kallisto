#include "kallisto/net/listener.hpp"
#include "kallisto/logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>

namespace kallisto {
namespace net {

namespace {

int createNonBlockingSocket() {
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        throw std::runtime_error("socket creation failed: " + std::string(strerror(errno)));
    }
    return socket_fd;
}

void configureAddressReuse(int fd) {
    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        close(fd);
        throw std::runtime_error("SO_REUSEADDR failed: " + std::string(strerror(errno)));
    }
}

void configurePortReuse(int fd) {
    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
        close(fd);
        throw std::runtime_error("SO_REUSEPORT failed: " + std::string(strerror(errno)));
    }
}

void configureSocketOptions(int fd, bool reuseport) {
    configureAddressReuse(fd);
    if (reuseport) {
        configurePortReuse(fd);
    }
}

void bindToPort(int fd, uint16_t port) {
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        throw std::runtime_error("bind failed on port " + std::to_string(port) + ": " + std::string(strerror(errno)));
    }
}

void startListening(int fd) {
    if (listen(fd, SOMAXCONN) < 0) {
        close(fd);
        throw std::runtime_error("listen failed: " + std::string(strerror(errno)));
    }
}

void handleAcceptFailureState() {
    // EAGAIN implies no network data available which is the expected healthy state boundary in edge-trigger loops.
    bool expected_empty_queue = (errno == EAGAIN || errno == EWOULDBLOCK);
    if (!expected_empty_queue) {
        // Logs hard socket layer failures (e.g. EMFILE out of file descriptors)
        // We do not throw to prevent the entire listener loop from indiscriminately crashing.
        error("[LISTENER] accept4 failed with hardware/system fault: " + std::string(strerror(errno)));
    }
}

} // anonymous namespace

int Listener::createListenSocket(uint16_t port, bool reuseport) {
    try {
        int socket_fd = createNonBlockingSocket();
        
        configureSocketOptions(socket_fd, reuseport);
        bindToPort(socket_fd, port);
        startListening(socket_fd);

        std::string mode = reuseport ? " (SO_REUSEPORT)" : "";
        info("[LISTENER] Listening on port " + std::to_string(port) + mode);
        
        return socket_fd;
    } catch (const std::exception& e) {
        error(std::string("[LISTENER] Critical startup failure: ") + e.what());
        throw; // Fail fast pattern: surface the exception to prevent zombie processes.
    }
}

int Listener::acceptConnection(int listen_fd, struct sockaddr_in* client_addr_out) {
    struct sockaddr_in current_client_addr{};
    socklen_t addr_len = sizeof(current_client_addr);
    
    // Explicit low-overhead accept inheriting non-blocking & security capabilities
    int client_fd = accept4(listen_fd, 
                            reinterpret_cast<struct sockaddr*>(&current_client_addr), 
                            &addr_len,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
    
    if (client_fd < 0) {
        handleAcceptFailureState();
        return -1; // Represents Null Object pattern identifying an "empty" connection slot
    }
    
    setTcpNoDelay(client_fd);
    
    if (client_addr_out) {
        *client_addr_out = current_client_addr;
    }
    
    return client_fd;
}

void Listener::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error("fcntl F_GETFL failed: " + std::string(strerror(errno)));
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error("fcntl F_SETFL failed: " + std::string(strerror(errno)));
    }
}

void Listener::setTcpNoDelay(int fd) {
    int enable = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) < 0) {
        error("[LISTENER] Failed to set TCP_NODELAY: " + std::string(strerror(errno)));
    }
}

void Listener::closeSocket(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

} // namespace net
} // namespace kallisto
