#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <iomanip>
#include "kallisto/kallisto.hpp"
#include "kallisto/logger.hpp"

std::unique_ptr<kallisto::KallistoServer> server;

void printHelp() {
    std::cout << "Kallisto Command Line Interface\n";
    std::cout << "Usage:\n";
    std::cout << "  PUT <path> <key> <value>   Store a secret\n";
    std::cout << "  GET <path> <key>           Retrieve a secret\n";
    std::cout << "  DEL <path> <key>           Delete a secret\n";
    std::cout << "  BENCH <count>              Run performance benchmark\n";
    std::cout << "  SAVE                       Force sync to disk\n";
    std::cout << "  MODE <STRICT|BATCH>        Set persistence mode\n";
    std::cout << "  LOGLEVEL <LEVEL>           Set log level (TRACE|DEBUG|INFO|WARN|ERROR|NONE)\n";
    std::cout << "  EXIT                       Quit\n";
}

void handlePut(std::stringstream& ss) {
    std::string path, key, value;
    ss >> path >> key;
    
    std::string rest;
    std::getline(ss, rest);
    size_t first = rest.find_first_not_of(' ');
    value = (first != std::string::npos) ? rest.substr(first) : rest;

    if (path.empty() || key.empty() || value.empty()) {
        kallisto::warn("PUT requires path, key, and value.");
        return;
    }

    if (server->putSecret(path, key, value)) {
        std::cout << "OK\n";
    } else {
        std::cout << "FAIL\n";
    }
}

void handleGet(std::stringstream& ss) {
    std::string path, key;
    ss >> path >> key;
    if (path.empty() || key.empty()) {
        kallisto::warn("GET requires path and key.");
        return;
    }

    std::string value = server->getSecret(path, key);
    if (value.empty()) {
        std::cout << "(nil)\n";
    } else {
        std::cout << value << "\n";
    }
}

void handleDelete(std::stringstream& ss) {
    std::string path, key;
    ss >> path >> key;
    if (server->deleteSecret(path, key)) {
        std::cout << "OK\n";
    } else {
        std::cout << "FAIL (Not found)\n";
    }
}

void runBenchmark(int count) {
    std::cout << "--- Starting Benchmark (" + std::to_string(count) + " ops) ---\n";
    
    auto prev_level = kallisto::Logger::getInstance().getLevel();
    kallisto::Logger::getInstance().setLevel(kallisto::LogLevel::NONE);
    
    std::cout << "[BENCH] Pre-generating data...\n";
    std::vector<std::string> paths, keys, vals;
    paths.reserve(count);
    keys.reserve(count);
    vals.reserve(count);

    for (int i = 0; i < count; ++i) {
        paths.push_back("/bench/p" + std::to_string(i % 10));
        keys.push_back("k" + std::to_string(i));
        vals.push_back("v" + std::to_string(i));
    }

    std::cout << "[BENCH] Running (logs disabled for accuracy)...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < count; ++i) {
        server->putSecret(paths[i], keys[i], vals[i]);
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    int hits = 0;
    for (int i = 0; i < count; ++i) {
        std::string val = server->getSecret(paths[i], keys[i]);
        if (!val.empty()) { hits++; }
    }

    auto end = std::chrono::high_resolution_clock::now();
    
    kallisto::Logger::getInstance().setLevel(prev_level);

    std::chrono::duration<double> write_sec = mid - start;
    std::chrono::duration<double> read_sec = end - mid;

    const double write_rps = count / write_sec.count();
    const double read_rps = count / read_sec.count();

    std::cout << "Write Time: " <<
      std::fixed << std::setprecision(4) << write_sec.count() << "s | RPS: " << write_rps << "\n";
    std::cout << "Read Time : " <<
      std::fixed << std::setprecision(4) << read_sec.count() << "s | RPS: " << read_rps << "\n";
    std::cout << "Hits      : " <<
      hits << "/" << count << "\n";
}

void processLine(const std::string& line) {
    std::stringstream ss(line);
    std::string cmd;
    ss >> cmd;

    for (auto & c: cmd) c = toupper(c);

    if (cmd == "PUT") { handlePut(ss);
    } else if (cmd == "GET") { handleGet(ss);
    } else if (cmd == "DEL") { handleDelete(ss);
    } else if (cmd == "BENCH") {
        int count;
        if (ss >> count) { runBenchmark(count);
        } else {
          kallisto::warn("Usage: BENCH <count>");
        }
    }
    else if (cmd == "SAVE") {
        server->force_save();
        std::cout << "OK (Saved to disk)\n";
    }
    else if (cmd == "MODE") {
        std::string mode_str;
        ss >> mode_str;
        for (auto & c: mode_str) c = toupper(c);

        if (mode_str == "STRICT" || mode_str == "IMMEDIATE") {
            server->setSyncMode(kallisto::KallistoServer::SyncMode::IMMEDIATE);
            std::cout << "OK (Mode: STRICT)\n";
        } else if (mode_str == "BATCH" || mode_str == "PERF") {
            server->setSyncMode(kallisto::KallistoServer::SyncMode::BATCH);
            std::cout << "OK (Mode: BATCH)\n";
        } else {
            kallisto::warn("Usage: MODE <STRICT|BATCH>");
        }
    }
    else if (cmd == "LOGLEVEL" || cmd == "LOG") {
        std::string level_str;
        ss >> level_str;
        if (level_str.empty()) {
            auto lvl = kallisto::Logger::getInstance().getLevel();
            const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "NONE"};
            std::cout << "Current log level: " << names[static_cast<int>(lvl)] << "\n";
        } else {
            kallisto::Logger::getInstance().setLevel(level_str);
            std::cout << "OK (Log level set)\n";
        }
    }
    else if (cmd == "EXIT" || cmd == "QUIT") {
        server.reset(); // Safely shut down the server before static destructors tear down Logger
        exit(0);
    }
    else if (cmd == "HELP") {
        printHelp();
    }
    else if (!cmd.empty()) {
        kallisto::warn("Unknown command. Type HELP.");
    }
}

int main(int argc, char** argv) {
    kallisto::Logger::getInstance().setLevel(kallisto::LogLevel::WARN);
    kallisto::Logger::getInstance().setThreadName("main");

    server = std::make_unique<kallisto::KallistoServer>();

    LOG_INFO("Kallisto Server Ready. Type HELP for commands.");
    
    std::string line;
    std::cout << "> ";
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
          continue;
        }
        processLine(line);
        std::cout << "> ";
    }

    return 0;
}
