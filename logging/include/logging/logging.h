#pragma once

#ifdef _WIN32
    #ifdef LOGGING_EXPORTS
        #define LOGGING_API __declspec(dllexport)
    #elif defined(LOGGING_IMPORTS)
        #define LOGGING_API __declspec(dllimport)
    #else
        #define LOGGING_API
    #endif
#else
    #define LOGGING_API
#endif

#include <string>
#include <atomic>
#include <thread>
#include <cstdint>

namespace jupiter {
namespace logging {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    FATAL = 4,
    NONE = 255
};

/**
 * @brief High-performance, lock-free logger for Project Jupiter
 *
 * Features:
 * - Zero-cost when disabled (compile-time)
 * - Tagged subsystem logging (Animation, Physics, Rendering, etc.)
 * - Multiple log levels with runtime filtering
 * - Lock-free design using atomic operations and thread-local buffers
 * - Dedicated logging thread for output
 * - Async message queuing for minimal latency
 * - File/console output redirection
 */
class LOGGING_API Logger {
public:
    /**
     * Get singleton instance (thread-safe)
     */
    static Logger& instance() noexcept;

    /**
     * Initialize the logging system
     * @param logFilePath Optional path to log file (empty string for console only)
     * @return true if initialization was successful, false otherwise
     */
    bool initialize(const std::string& logFilePath = "");

    /**
     * Shutdown the logging system
     */
    void shutdown();

    /**
     * Log a message with tag and level (internal use - called by macros)
     */
    void log(LogLevel level, const char* tag, const char* file, int line,
             const char* fmt, ...) noexcept __attribute__((format(printf, 6, 7)));

    /**
     * Set minimum log level (messages below this level are ignored)
     * @param level Minimum log level
     */
    void setLogLevel(LogLevel level) noexcept;

    /**
     * Get current minimum log level
     * @return Current minimum log level
     */
    LogLevel getLogLevel() const noexcept;

    /**
     * Enable/disable a specific subsystem tag
     * @param tag Tag to enable/disable (e.g., "Physics", "Rendering")
     * @param enabled true to enable, false to disable
     */
    void setTagEnabled(const char* tag, bool enabled) noexcept;

    /**
     * Check if a tag is enabled
     * @param tag Tag to check
     * @return true if enabled, false otherwise
     */
    bool isTagEnabled(const char* tag) const noexcept;

    /**
     * Set output file path (thread-safe)
     * @param path File path (empty string for console only)
     * @return true on success
     */
    bool setOutputFile(const std::string& path);

    /**
     * Flush pending log messages (forces immediate output)
     */
    void flush();

    // Legacy API for backward compatibility
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

private:
    Logger() noexcept;
    ~Logger() noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Lock-free message queue
    struct LogMessage {
        LogLevel level;
        char tag[16];
        char file[64];
        int line;
        char message[512];
        uint64_t timestamp;
        uint64_t thread_id;
    };

    static constexpr size_t MESSAGE_QUEUE_SIZE = 1024;
    std::atomic<size_t> write_index_{0};
    std::atomic<size_t> read_index_{0};
    LogMessage message_queue_[MESSAGE_QUEUE_SIZE];

    // Configuration (atomics for lock-free access)
    std::atomic<LogLevel> min_level_{LogLevel::DEBUG};
    std::atomic<bool> initialized_{false};

    // Output configuration
    std::atomic<FILE*> output_file_{nullptr};
    std::string output_path_;

    // Tag filtering
    static constexpr size_t MAX_TAGS = 32;
    struct TagFilter {
        char name[16];
        std::atomic<bool> enabled{true};
    };
    TagFilter tag_filters_[MAX_TAGS];
    std::atomic<size_t> tag_count_{0};

    // Logging thread
    std::atomic<bool> running_{false};
    std::thread logging_thread_;

    // Helper functions
    bool pushMessage(const LogMessage& msg) noexcept;
    bool popMessage(LogMessage& msg) noexcept;
    void loggingThreadFunc();
    void formatMessage(const LogMessage& msg, char* buffer, size_t size) const noexcept;
    void writeToOutput(const char* buffer, size_t size) noexcept;
    const char* levelToString(LogLevel level) const noexcept;
    uint64_t getCurrentTimestamp() const noexcept;
    uint64_t getCurrentThreadId() const noexcept;
};

/**
 * @brief Initialize the logging system (legacy API)
 */
LOGGING_API bool initialize(const std::string& logFilePath = "");

/**
 * @brief Shutdown the logging system (legacy API)
 */
LOGGING_API void shutdown();

// ============================================================================
// High-Performance Logging Macros
// ============================================================================

#ifdef JUPITER_ENABLE_LOGGING

#define JUPITER_LOG(level, tag, fmt, ...) \
    ::jupiter::logging::Logger::instance().log( \
        level, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(tag, fmt, ...) JUPITER_LOG(::jupiter::logging::LogLevel::DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  JUPITER_LOG(::jupiter::logging::LogLevel::INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  JUPITER_LOG(::jupiter::logging::LogLevel::WARNING, tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) JUPITER_LOG(::jupiter::logging::LogLevel::ERROR, tag, fmt, ##__VA_ARGS__)
#define LOG_FATAL(tag, fmt, ...) JUPITER_LOG(::jupiter::logging::LogLevel::FATAL, tag, fmt, ##__VA_ARGS__)

// Legacy macros for backward compatibility
#define LOGD(msg) ::jupiter::logging::Logger::instance().debug(msg)
#define LOGI(msg) ::jupiter::logging::Logger::instance().info(msg)
#define LOGW(msg) ::jupiter::logging::Logger::instance().warning(msg)
#define LOGE(msg) ::jupiter::logging::Logger::instance().error(msg)
#define LOGF(msg) ::jupiter::logging::Logger::instance().fatal(msg)

#else

// When logging is disabled, macros compile to nothing (zero overhead)
#define JUPITER_LOG(level, tag, fmt, ...) ((void)0)
#define LOG_DEBUG(tag, fmt, ...) ((void)0)
#define LOG_INFO(tag, fmt, ...)  ((void)0)
#define LOG_WARN(tag, fmt, ...)  ((void)0)
#define LOG_ERROR(tag, fmt, ...) ((void)0)
#define LOG_FATAL(tag, fmt, ...) ((void)0)

#define LOGD(msg) ((void)0)
#define LOGI(msg) ((void)0)
#define LOGW(msg) ((void)0)
#define LOGE(msg) ((void)0)
#define LOGF(msg) ((void)0)

#endif // JUPITER_ENABLE_LOGGING

} // namespace logging
} // namespace jupiter
