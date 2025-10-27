#include "logging/logging.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <sys/time.h>

#ifdef __APPLE__
#include <pthread.h>
#endif

namespace jupiter {
namespace logging {

namespace {
    std::ofstream logFile;
    LogLevel minLogLevel = LogLevel::DEBUG;
    bool initialized = false;

    std::string getCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string logLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
}

bool initialize(const std::string& logFilePath) {
    if (initialized) {
        return true;
    }

    if (!logFilePath.empty()) {
        logFile.open(logFilePath, std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file: " << logFilePath << std::endl;
            return false;
        }
    }

    initialized = true;
    Logger::instance().info("Logging system initialized");
    return true;
}

void shutdown() {
    if (!initialized) {
        return;
    }

    Logger::instance().info("Logging system shutdown");

    if (logFile.is_open()) {
        logFile.close();
    }

    initialized = false;
}

void log(LogLevel level, const std::string& message) {
    if (!initialized || level < minLogLevel) {
        return;
    }

    std::string timestamp = getCurrentTimeString();
    std::string levelStr = logLevelToString(level);

    std::string formattedMessage = "[" + timestamp + "] [" + levelStr + "] " + message;

    // Output to console
    std::cout << formattedMessage << std::endl;

    // Output to file if available
    if (logFile.is_open()) {
        logFile << formattedMessage << std::endl;
        logFile.flush();
    }
}

void debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void fatal(const std::string& message) {
    log(LogLevel::FATAL, message);
}

void setLogLevel(LogLevel level) {
    minLogLevel = level;
}

// ============================================================================
// Logger Class Implementation
// ============================================================================

Logger& Logger::instance() noexcept {
    static Logger instance;
    return instance;
}

Logger::Logger() noexcept {}

Logger::~Logger() noexcept {}

void Logger::log(LogLevel level, const char* tag, const char* file, int line,
                 const char* fmt, ...) noexcept {
    if (!initialized || level < minLogLevel) {
        return;
    }

    // Format the message using variable arguments
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::string timestamp = getCurrentTimeString();
    std::string levelStr = logLevelToString(level);

    std::string formattedMessage = "[" + timestamp + "] [" + levelStr + "] [" +
                                  std::string(tag) + "] " + std::string(buffer);

    // Output to console
    std::cout << formattedMessage << std::endl;

    // Output to file if available
    if (logFile.is_open()) {
        logFile << formattedMessage << std::endl;
        logFile.flush();
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, "Legacy", "", 0, "%s", message.c_str());
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, "Legacy", "", 0, "%s", message.c_str());
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, "Legacy", "", 0, "%s", message.c_str());
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, "Legacy", "", 0, "%s", message.c_str());
}

void Logger::fatal(const std::string& message) {
    log(LogLevel::FATAL, "Legacy", "", 0, "%s", message.c_str());
}

} // namespace logging
} // namespace jupiter
