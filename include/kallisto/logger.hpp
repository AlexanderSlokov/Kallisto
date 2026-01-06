#pragma once

#include <string>
#include <iostream>
#include <string_view>
#include <memory>

namespace kallisto {

enum class LogFormat { Standard };

struct LogConfig {
    std::string name;
    std::string logLevel = "info";
    std::string logFilePath;
    int logRotateBytes = 0;
    int logRotateMaxFiles = 0;
    LogConfig(std::string_view name) : name(name) {}
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    void setup(const LogConfig& config) {}
};

inline void info(const std::string& msg) {
    std::cout << "[INFO] " << msg << std::endl;
}

inline void error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}

inline void debug(const std::string& msg) {
    std::cout << "[DEBUG] " << msg << std::endl;
}

inline void warn(const std::string& msg) {
    std::cout << "[WARN] " << msg << std::endl;
}

} // namespace kallisto