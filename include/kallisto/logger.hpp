#pragma once

#include <string>
#include <iostream>
#include <string_view>
#include <memory>

namespace kallisto {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };

struct LogConfig {
    std::string name;
    std::string logLevel = "info";
    std::string logFilePath;
    int logRotateBytes = 0;
    int logRotateMaxFiles = 0;
    LogConfig(std::string_view name) : name(name) {}
};

class Logger {
    LogLevel currentLevel = LogLevel::INFO;
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setup(const LogConfig& config) {
        std::string lvl = config.logLevel;
        // Simple case-insensitive parser
        for (auto & c: lvl) c = tolower(c);
        
        if (lvl == "debug") currentLevel = LogLevel::DEBUG;
        else if (lvl == "info") currentLevel = LogLevel::INFO;
        else if (lvl == "warn") currentLevel = LogLevel::WARN;
        else if (lvl == "error") currentLevel = LogLevel::ERROR;
        else currentLevel = LogLevel::INFO; // Default
    }

    bool shouldLog(LogLevel level) const {
        return level >= currentLevel;
    }
};

inline void info(const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::INFO))
        std::cout << "[INFO] " << msg << std::endl;
}

inline void error(const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::ERROR))
        std::cerr << "[ERROR] " << msg << std::endl;
}

inline void debug(const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::DEBUG))
        std::cout << "[DEBUG] " << msg << std::endl;
}

inline void warn(const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::WARN))
        std::cout << "[WARN] " << msg << std::endl;
}

} // namespace kallisto