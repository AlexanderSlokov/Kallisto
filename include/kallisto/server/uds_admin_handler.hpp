#pragma once

#include "kallisto/kallisto_core.hpp"
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace kallisto {
namespace server {

class UdsAdminHandler {
public:
    UdsAdminHandler(std::shared_ptr<KallistoCore> core, 
                    const std::string& socket_path = "/var/run/kallisto.sock");
    ~UdsAdminHandler();

    void start();
    void stop();

private:
    void acceptLoop();
    void handleClient(int client_fd);

    std::shared_ptr<KallistoCore> core_;
    std::string socket_path_;
    int server_fd_{-1};
    std::thread accept_thread_;
    std::atomic<bool> running_{false};
};

} // namespace server
} // namespace kallisto
