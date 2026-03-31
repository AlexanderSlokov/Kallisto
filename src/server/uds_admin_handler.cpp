#include "kallisto/server/uds_admin_handler.hpp"
#include "kallisto/logger.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>

namespace kallisto {
namespace server {

UdsAdminHandler::UdsAdminHandler(std::shared_ptr<KallistoCore> core, 
                                 const std::string& socket_path)
    : core_(std::move(core)), socket_path_(socket_path) {
}

UdsAdminHandler::~UdsAdminHandler() {
    stop();
}

void UdsAdminHandler::start() {
    if (running_) {
        return;
    }

    // 1. Unlink before bind to prevent Zombie Socket Trap
    ::unlink(socket_path_.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        error("[UDS Admin] Failed to create socket");
        return;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        error("[UDS Admin] Failed to bind to " + socket_path_);
        ::close(server_fd_);
        server_fd_ = -1;
        return;
    }

    // 2. Chmod 0600 immediately after bind (OS-LEVEL CONSTRAINT)
    if (::chmod(socket_path_.c_str(), 0600) < 0) {
        error("[UDS Admin] Failed to chmod 0600 on " + socket_path_);
        ::close(server_fd_);
        server_fd_ = -1;
        return;
    }

    if (::listen(server_fd_, 5) < 0) {
        error("[UDS Admin] Failed to listen on " + socket_path_);
        ::close(server_fd_);
        server_fd_ = -1;
        return;
    }

    running_ = true;
    accept_thread_ = std::thread(&UdsAdminHandler::acceptLoop, this);
    info("[UDS Admin] Listening on " + socket_path_ + " (Permissions: 0600)");
}

void UdsAdminHandler::stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    // By closing the socket, accept() will unblock and fail
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR); // Kick accept() out
        ::close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    ::unlink(socket_path_.c_str());
    info("[UDS Admin] Stopped.");
}

void UdsAdminHandler::acceptLoop() {
    while (running_) {
        int client_fd = ::accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_) {
                warn("[UDS Admin] Accept failed");
            }
            continue;
        }

        handleClient(client_fd);
    }
}

void UdsAdminHandler::handleClient(int client_fd) {
    char buf[1024];
    ssize_t bytes = ::recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (bytes <= 0) {
        ::close(client_fd);
        return;
    }

    buf[bytes] = '\0';
    std::string cmd(buf);

    // Trim whitespace/newlines
    cmd.erase(0, cmd.find_first_not_of(" \t\r\n"));
    cmd.erase(cmd.find_last_not_of(" \t\r\n") + 1);

    std::string response = "UNKNOWN COMMAND\n";

    if (cmd == "SAVE") {
        core_->force_flush();
        kallisto::info("[UDS Admin] Invoked manual SAVE.");
        response = "OK: Database flushed to disk.\n";
    } else if (cmd == "MODE BATCH") {
        core_->change_sync_mode(KallistoCore::SyncMode::BATCH);
        kallisto::info("[UDS Admin] Sync mode changed to BATCH.");
        response = "OK: Mode changed to BATCH.\n";
    } else if (cmd == "MODE IMMEDIATE") {
        core_->change_sync_mode(KallistoCore::SyncMode::IMMEDIATE);
        kallisto::info("[UDS Admin] Sync mode changed to IMMEDIATE.");
        response = "OK: Mode changed to IMMEDIATE.\n";
    }

    ::send(client_fd, response.c_str(), response.size(), 0);
    ::close(client_fd);
}

} // namespace server
} // namespace kallisto
