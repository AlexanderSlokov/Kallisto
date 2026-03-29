#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

void print_help() {
    std::cerr << "Kallisto Admin CLI (Dumb Client)\n"
              << "Usage: kallisto <COMMAND>\n"
              << "Commands:\n"
              << "  SAVE             Force flush RocksDB to disk\n"
              << "  MODE BATCH       Switch to asynchronous batch persistence\n"
              << "  MODE IMMEDIATE   Switch to synchronous strict persistence\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    std::string cmd = argv[1];
    for (int i = 2; i < argc; ++i) {
        cmd += " ";
        cmd += argv[i];
    }

    if (cmd == "--help" || cmd == "-h") {
        print_help();
        return 0;
    }

    std::string socket_path = "/var/run/kallisto.sock";

    int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Error: Failed to create UNIX socket.\n";
        return 1;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error: Failed to connect to Kallisto Admin Socket at " << socket_path << ".\n"
                  << "Is the server running? Do you have sudo/owner privileges or is the correct socket path configured?\n";
        ::close(sock);
        return 1;
    }

    // Send command
    if (::send(sock, cmd.c_str(), cmd.size(), 0) < 0) {
        std::cerr << "Error: Failed to send command to server.\n";
        ::close(sock);
        return 1;
    }

    // Receive response
    char buf[1024];
    ssize_t bytes = ::recv(sock, buf, sizeof(buf) - 1, 0);
    if (bytes < 0) {
        std::cerr << "Error: Failed to read response from server.\n";
        ::close(sock);
        return 1;
    } else if (bytes == 0) {
        std::cerr << "Error: Server closed connection.\n";
        ::close(sock);
        return 1;
    }

    buf[bytes] = '\0';
    std::cout << buf;

    ::close(sock);
    return 0;
}
