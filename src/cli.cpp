/**
 * Kallisto Admin CLI
 * 
 * A lightweight client that connects to the Kallisto server's
 * Unix Domain Socket to issue administrative commands.
 * 
 * Examples:
 *   kallisto SAVE
 *   kallisto MODE BATCH
 *   kallisto MODE IMMEDIATE
 */

#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr const char* default_socket_path = "/var/run/kallisto.sock";
static constexpr size_t receive_buffer_size = 1024;

// ---------------------------------------------------------------------------
// RAII Socket Guard — prevents file descriptor leaks on all exit paths.
// ---------------------------------------------------------------------------
class SocketGuard {
public:
  explicit SocketGuard(int fd) : fd_(fd) {}
  ~SocketGuard() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  SocketGuard(const SocketGuard&) = delete;
  SocketGuard& operator=(const SocketGuard&) = delete;

  int fd() const { return fd_; }
  bool isValid() const { return fd_ >= 0; }

private:
  int fd_;
};

// ---------------------------------------------------------------------------
// CLI Functions — each does exactly one thing.
// ---------------------------------------------------------------------------

void printUsage() {
  std::cerr << "Kallisto Admin CLI\n"
            << "Usage: kallisto <COMMAND>\n"
            << "Commands:\n"
            << "  SAVE             Force flush RocksDB to disk\n"
            << "  MODE BATCH       Switch to asynchronous batch persistence\n"
            << "  MODE IMMEDIATE   Switch to synchronous strict persistence\n";
}

std::string buildCommandFromArgs(int argc, char** argv) {
  std::string command = argv[1];
  for (int i = 2; i < argc; ++i) {
    command += " ";
    command += argv[i];
  }
  return command;
}

bool isHelpRequested(const std::string& first_arg) {
  return first_arg == "--help" || first_arg == "-h";
}

int connectToAdminSocket(const char* socket_path) {
  int sock_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    return -1;
  }

  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

  if (::connect(sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(sock_fd);
    return -1;
  }

  return sock_fd;
}

bool sendCommand(int sock_fd, const std::string& command) {
  return ::send(sock_fd, command.c_str(), command.size(), 0) >= 0;
}

std::string receiveResponse(int sock_fd) {
  char buffer[receive_buffer_size];
  ssize_t bytes_received = ::recv(sock_fd, buffer, sizeof(buffer) - 1, 0);

  if (bytes_received <= 0) {
    return "";
  }

  buffer[bytes_received] = '\0';
  return std::string(buffer);
}

// ---------------------------------------------------------------------------
// Entry Point
// ---------------------------------------------------------------------------
#ifndef KALLISTO_CLI_TEST_MODE
int main(int argc, char** argv) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  if (isHelpRequested(argv[1])) {
    printUsage();
    return 0;
  }

  std::string command = buildCommandFromArgs(argc, argv);

  int sock_fd = connectToAdminSocket(default_socket_path);
  if (sock_fd < 0) {
    std::cerr << "Error: Failed to connect to Kallisto Admin Socket at "
              << default_socket_path << ".\n"
              << "Is the server running? Do you have the correct privileges?\n";
    return 1;
  }

  SocketGuard guard(sock_fd);

  if (!sendCommand(guard.fd(), command)) {
    std::cerr << "Error: Failed to send command to server.\n";
    return 1;
  }

  std::string response = receiveResponse(guard.fd());
  if (response.empty()) {
    std::cerr << "Error: No response from server (connection closed or recv failed).\n";
    return 1;
  }

  std::cout << response;
  return 0;
}
#endif
