#include <gtest/gtest.h>
#include "kallisto/server/uds_admin_handler.hpp"
#include "kallisto/kallisto_core.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <string>

using namespace kallisto;
using namespace kallisto::server;

class UdsAdminTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create core (using temporary db path)
        std::string db_path = "/tmp/kallisto_test_db_" + std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
        core = std::make_shared<KallistoCore>(db_path);
        
        socket_path = "/tmp/kallisto_test_admin.sock";
        
        // Clean up socket if previous test crashed
        if (std::filesystem::exists(socket_path)) {
            std::filesystem::remove(socket_path);
        }
    }

    void TearDown() override {
        if (handler) {
            handler->stop();
        }
        if (std::filesystem::exists(socket_path)) {
            std::filesystem::remove(socket_path);
        }
        core.reset();
    }

    std::string sendCommand(const std::string& cmd) {
        int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) return "SOCKET_ERROR";

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(sock);
            return "CONNECT_ERROR";
        }

        if (::send(sock, cmd.c_str(), cmd.size(), 0) < 0) {
            ::close(sock);
            return "SEND_ERROR";
        }

        char buf[1024];
        ssize_t bytes = ::recv(sock, buf, sizeof(buf) - 1, 0);
        ::close(sock);

        if (bytes < 0) return "RECV_ERROR";
        return std::string(buf, bytes);
    }

    std::shared_ptr<KallistoCore> core;
    std::string socket_path;
    std::unique_ptr<UdsAdminHandler> handler;
};

// Test 1: Happy Path
TEST_F(UdsAdminTest, HappyPathSave) {
    handler = std::make_unique<UdsAdminHandler>(core, socket_path);
    handler->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Wait for listen

    std::string resp = sendCommand("SAVE");
    EXPECT_EQ(resp, "OK: Database flushed to disk.\n");
}

// Test 2: Bad Command
TEST_F(UdsAdminTest, BadCommand) {
    handler = std::make_unique<UdsAdminHandler>(core, socket_path);
    handler->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sendCommand("MAKE_ME_COFFEE");
    EXPECT_EQ(resp, "UNKNOWN COMMAND\n");
}

// Test 3: Whitespace Tolerance
TEST_F(UdsAdminTest, WhitespaceTolerance) {
    handler = std::make_unique<UdsAdminHandler>(core, socket_path);
    handler->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string resp = sendCommand("   MODE BATCH   \n");
    EXPECT_EQ(resp, "OK: Mode changed to BATCH.\n");
}

// Test 4: Resource Cleanup (Zombie Socket Prevention)
TEST_F(UdsAdminTest, ResourceCleanup) {
    handler = std::make_unique<UdsAdminHandler>(core, socket_path);
    handler->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(std::filesystem::exists(socket_path));
    
    handler->stop();
    // After stop, the file should be unlinked
    EXPECT_FALSE(std::filesystem::exists(socket_path));
}
