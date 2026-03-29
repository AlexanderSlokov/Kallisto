#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>

namespace kallisto {

enum class LogLevel { TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, NONE = 5 };

// Forward declaration for backward compatibility
struct LogConfig;

/**
 * High-performance logger with:
 * - Compile-time level filtering (zero-cost when disabled)
 * - Runtime level filtering
 * - Thread-safe output
 * - Structured format: [TIMESTAMP] [LEVEL] [TID:xxx] [file:line] message
 * 
 * Usage:
 *   LOG_INFO("Server started on port {}", port);
 *   LOG_DEBUG("Processing request");
 * 
 * For benchmarks, set level to NONE or use KALLISTO_LOG_LEVEL_NONE compile flag.
 */
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setLevel(LogLevel level) { 
        current_level_.store(level, std::memory_order_relaxed); 
    }
    
    void setLevel(const std::string& level_str) {
        std::string lvl = level_str;
        for (auto& c : lvl) c = std::tolower(c);
        
        if (lvl == "trace") setLevel(LogLevel::TRACE);
        else if (lvl == "debug") setLevel(LogLevel::DEBUG);
        else if (lvl == "info") setLevel(LogLevel::INFO);
        else if (lvl == "warn" || lvl == "warning") setLevel(LogLevel::WARN);
        else if (lvl == "error") setLevel(LogLevel::ERROR);
        else if (lvl == "none" || lvl == "off") setLevel(LogLevel::NONE);
        else setLevel(LogLevel::INFO);
    }

    LogLevel getLevel() const { 
        return current_level_.load(std::memory_order_relaxed); 
    }

    bool shouldLog(LogLevel level) const {
#ifdef KALLISTO_LOG_LEVEL_NONE
        return false; // Compile-time disable for max benchmark perf
#else
        return level >= current_level_.load(std::memory_order_relaxed);
#endif
    }

    void setThreadName(const std::string& name) {
        thread_name_ = name;
    }

    const std::string& getThreadName() const {
        return thread_name_;
    }

    void log(LogLevel level, const char* file, int line, const std::string& msg) {
        if (!shouldLog(level)) return;

        // Extract filename from path
        const char* filename = file;
        if (const char* slash = std::strrchr(file, '/')) {
            filename = slash + 1;
        } else if (const char* bslash = std::strrchr(file, '\\')) {
            filename = bslash + 1;
        }

        // Format timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf;
        gmtime_r(&time_t_now, &tm_buf);

        std::ostringstream oss;
        oss << '[' 
            << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z] ";

        // Level
        oss << '[' << levelToString(level) << "] ";

        // Thread ID (use name if set, otherwise numeric)
        if (!thread_name_.empty()) {
            oss << "[tid:" << thread_name_ << "] ";
        } else {
            oss << "[tid:" << std::this_thread::get_id() << "] ";
        }

        // File:line
        oss << '[' << filename << ':' << line << "] ";

        // Message
        oss << msg;

        // Thread-safe output
        std::lock_guard<std::mutex> lock(output_mutex_);
        if (level >= LogLevel::ERROR) {
            std::cerr << oss.str() << std::endl;
        } else {
            std::clog << oss.str() << std::endl;
        }
    }

private:
    Logger() : current_level_(LogLevel::INFO) {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

public:
    // Backward compatibility with LogConfig - defined after LogConfig struct
    void setup(const LogConfig& config);

private:

    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default: return "?????";
        }
    }

    std::atomic<LogLevel> current_level_;
    std::mutex output_mutex_;
    static thread_local std::string thread_name_;
};

// Thread-local storage for thread name
inline thread_local std::string Logger::thread_name_;

// ============================================================================
// Logging Macros - Zero-cost when level is disabled
// ============================================================================

#ifdef KALLISTO_LOG_LEVEL_NONE
    // Compile-time disable: all logging becomes no-op
    #define LOG_TRACE(...) ((void)0)
    #define LOG_DEBUG(...) ((void)0)
    #define LOG_INFO(...)  ((void)0)
    #define LOG_WARN(...)  ((void)0)
    #define LOG_ERROR(...) ((void)0)
#else
    // Runtime checking with short-circuit evaluation
    #define LOG_TRACE(msg) \
        do { if (::kallisto::Logger::getInstance().shouldLog(::kallisto::LogLevel::TRACE)) \
            ::kallisto::Logger::getInstance().log(::kallisto::LogLevel::TRACE, __FILE__, __LINE__, msg); } while(0)

    #define LOG_DEBUG(msg) \
        do { if (::kallisto::Logger::getInstance().shouldLog(::kallisto::LogLevel::DEBUG)) \
            ::kallisto::Logger::getInstance().log(::kallisto::LogLevel::DEBUG, __FILE__, __LINE__, msg); } while(0)

    #define LOG_INFO(msg) \
        do { if (::kallisto::Logger::getInstance().shouldLog(::kallisto::LogLevel::INFO)) \
            ::kallisto::Logger::getInstance().log(::kallisto::LogLevel::INFO, __FILE__, __LINE__, msg); } while(0)

    #define LOG_WARN(msg) \
        do { if (::kallisto::Logger::getInstance().shouldLog(::kallisto::LogLevel::WARN)) \
            ::kallisto::Logger::getInstance().log(::kallisto::LogLevel::WARN, __FILE__, __LINE__, msg); } while(0)

    #define LOG_ERROR(msg) \
        do { if (::kallisto::Logger::getInstance().shouldLog(::kallisto::LogLevel::ERROR)) \
            ::kallisto::Logger::getInstance().log(::kallisto::LogLevel::ERROR, __FILE__, __LINE__, msg); } while(0)
#endif

// ============================================================================
// Legacy API (for backward compatibility)
// ============================================================================

struct LogConfig {
    std::string name;
    std::string logLevel = "info";
    std::string logFilePath;
    int logRotateBytes = 0;
    int logRotateMaxFiles = 0;
    LogConfig(std::string_view name) : name(name) {}
};

// Define Logger::setup() now that LogConfig is complete
inline void Logger::setup(const LogConfig& config) {
    setLevel(config.logLevel);
}

// Legacy inline functions with explicit file/line parameters
inline void trace_impl(const char* file, int line, const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::TRACE))
        Logger::getInstance().log(LogLevel::TRACE, file, line, msg);
}
inline void debug_impl(const char* file, int line, const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::DEBUG))
        Logger::getInstance().log(LogLevel::DEBUG, file, line, msg);
}
inline void info_impl(const char* file, int line, const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::INFO))
        Logger::getInstance().log(LogLevel::INFO, file, line, msg);
}
inline void warn_impl(const char* file, int line, const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::WARN))
        Logger::getInstance().log(LogLevel::WARN, file, line, msg);
}
inline void error_impl(const char* file, int line, const std::string& msg) {
    if (Logger::getInstance().shouldLog(LogLevel::ERROR))
        Logger::getInstance().log(LogLevel::ERROR, file, line, msg);
}

// Legacy wrapper functions (for existing code using kallisto::warn("msg") style)
// Note: These will show logger.hpp as file location. For correct file:line, use LOG_* macros.
inline void trace(const std::string& msg) { trace_impl(__FILE__, __LINE__, msg); }
inline void debug(const std::string& msg) { debug_impl(__FILE__, __LINE__, msg); }
inline void info(const std::string& msg)  { info_impl(__FILE__, __LINE__, msg); }
inline void warn(const std::string& msg)  { warn_impl(__FILE__, __LINE__, msg); }
inline void error(const std::string& msg) { error_impl(__FILE__, __LINE__, msg); }

} // namespace kallisto

// Macro versions that capture correct file:line (use these for new code)
#define KALLISTO_TRACE(msg) ::kallisto::trace_impl(__FILE__, __LINE__, msg)
#define KALLISTO_DEBUG(msg) ::kallisto::debug_impl(__FILE__, __LINE__, msg)
#define KALLISTO_INFO(msg)  ::kallisto::info_impl(__FILE__, __LINE__, msg)
#define KALLISTO_WARN(msg)  ::kallisto::warn_impl(__FILE__, __LINE__, msg)
#define KALLISTO_ERROR(msg) ::kallisto::error_impl(__FILE__, __LINE__, msg)