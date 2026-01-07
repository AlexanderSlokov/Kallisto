#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <iomanip>
#include "kallisto/kallisto.hpp"
#include "kallisto/logger.hpp"

// Ideally, the server might be heavy (large memory allocation for tables).
// Managing it via unique_ptr ensures clean shutdown and allows for lazy initialization if needed.
std::unique_ptr<kallisto::KallistoServer> server;

void print_help() {
    std::cout << "Kallisto Command Line Interface\n";
    std::cout << "Usage:\n";
    std::cout << "  PUT <path> <key> <value>   Store a secret\n";
    std::cout << "  GET <path> <key>           Retrieve a secret\n";
    std::cout << "  DEL <path> <key>           Delete a secret\n";
    std::cout << "  BENCH <count>              Run performance benchmark\n";
    std::cout << "  SAVE                       Force sync to disk\n";
    std::cout << "  MODE <STRICT|BATCH>        Set persistence mode\n";
    std::cout << "  EXIT                       Quit\n";
}

void handle_put(std::stringstream& ss) {
    std::string path, key, value;
    ss >> path >> key;
    // Value might contain spaces? For MVP, we assume no spaces or read rest of line.
    // Let's read the rest of the line to allow "some secret value"
    std::string rest;
    std::getline(ss, rest);
    
    // Trim leading whitespace from getline
    size_t first = rest.find_first_not_of(' ');
    if (std::string::npos != first) {
        value = rest.substr(first);
    } else {
        value = rest; 
    }

    if (path.empty() || key.empty() || value.empty()) {
        kallisto::warn("PUT requires path, key, and value.");
        return;
    }

    if (server->put_secret(path, key, value)) {
        std::cout << "OK\n";
    } else {
        std::cout << "FAIL\n";
    }
}

void handle_get(std::stringstream& ss) {
    std::string path, key;
    ss >> path >> key;
    if (path.empty() || key.empty()) {
        kallisto::warn("GET requires path and key.");
        return;
    }

    std::string value = server->get_secret(path, key);
    if (value.empty()) {
        std::cout << "(nil)\n";
    } else {
        std::cout << value << "\n";
    }
}

void handle_del(std::stringstream& ss) {
    std::string path, key;
    ss >> path >> key;
    if (server->delete_secret(path, key)) {
        std::cout << "OK\n";
    } else {
        std::cout << "FAIL (Not found)\n";
    }
}

void run_benchmark(int count) {
    std::cout << "--- Starting Benchmark (" + std::to_string(count) + " ops) ---\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 1. Write Phase
    // Why sequential paths? To test B-Tree split logic heavily.
    // Why random keys? To test SipHash distribution.
    for (int i = 0; i < count; ++i) {
        // Use a few fixed paths to simulate real grouping
        std::string path = "/bench/p" + std::to_string(i % 10); 
        std::string key = "k" + std::to_string(i);
        std::string val = "v" + std::to_string(i);
        server->put_secret(path, key, val);
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    // 2. Read Phase (Hot path)
    int hits = 0;
    for (int i = 0; i < count; ++i) {
        std::string path = "/bench/p" + std::to_string(i % 10); 
        std::string key = "k" + std::to_string(i);
        std::string val = server->get_secret(path, key);
        if (!val.empty()) hits++;
    }

    auto end = std::chrono::high_resolution_clock::now();

    // Stats Calculation
    std::chrono::duration<double> write_sec = mid - start;
    std::chrono::duration<double> read_sec = end - mid;
    
    double write_rps = count / write_sec.count();
    double read_rps = count / read_sec.count();

    std::cout << "Write Time: " << std::fixed << std::setprecision(4) << write_sec.count() << "s | RPS: " << write_rps << "\n";
    std::cout << "Read Time : " << std::fixed << std::setprecision(4) << read_sec.count() << "s | RPS: " << read_rps << "\n";
    std::cout << "Hits      : " << hits << "/" << count << "\n";
}

void process_line(std::string line) {
    std::stringstream ss(line);
    std::string cmd;
    ss >> cmd;

    // Normalize command to uppercase
    for (auto & c: cmd) c = toupper(c);

    if (cmd == "PUT") handle_put(ss);
    else if (cmd == "GET") handle_get(ss);
    else if (cmd == "DEL") handle_del(ss);
    else if (cmd == "BENCH") {
        int count;
        if (ss >> count) run_benchmark(count);
        else kallisto::warn("Usage: BENCH <count>");
    }
    else if (cmd == "SAVE") {
        server->force_save();
        std::cout << "OK (Saved to disk)\n";
    }
    else if (cmd == "MODE") {
        std::string mode_str;
        ss >> mode_str;
        // toupper mode_str
        for (auto & c: mode_str) c = toupper(c);

        if (mode_str == "STRICT" || mode_str == "IMMEDIATE") {
            server->set_sync_mode(kallisto::KallistoServer::SyncMode::IMMEDIATE);
            std::cout << "OK (Mode: STRICT)\n";
        } else if (mode_str == "BATCH" || mode_str == "PERF") {
            server->set_sync_mode(kallisto::KallistoServer::SyncMode::BATCH);
            std::cout << "OK (Mode: BATCH)\n";
        } else {
            kallisto::warn("Usage: MODE <STRICT|BATCH>");
        }
    }
    else if (cmd == "EXIT" || cmd == "QUIT") {
        exit(0);
    }
    else if (cmd == "HELP") {
        print_help();
    }
    else if (!cmd.empty()) {
        kallisto::warn("Unknown command. Type HELP.");
    }
}

int main(int argc, char** argv) {
    // Initialize Server
    // Why standard logs? We want to see startup logs in the file/stderr 
    // but keep stdout clean for CLI responses (GET values).
    kallisto::LogConfig config("kallisto.server.log");
    config.logFilePath = "logs/";
    // Set to WARN to avoid spamming the CLI during benchmark, 
    // unless DEBUG is needed.
    config.logLevel = "warn"; 
    kallisto::Logger::getInstance().setup(config);

    server = std::make_unique<kallisto::KallistoServer>();

    // Interactive Mode
    kallisto::info("Kallisto Server Ready. Type HELP for commands.");
    
    std::string line;
    std::cout << "> ";
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        process_line(line);
        std::cout << "> ";
    }

    return 0;
}
