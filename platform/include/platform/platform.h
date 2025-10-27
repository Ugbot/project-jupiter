#pragma once

#ifdef _WIN32
    #ifdef PLATFORM_EXPORTS
        #define PLATFORM_API __declspec(dllexport)
    #elif defined(PLATFORM_IMPORTS)
        #define PLATFORM_API __declspec(dllimport)
    #else
        #define PLATFORM_API
    #endif
#else
    #define PLATFORM_API
#endif

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Platform detection macros
#if defined(_WIN32)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
    #define PLATFORM_MACOS 1
    #define PLATFORM_NAME "macOS"
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
    #define PLATFORM_NAME "Linux"
#else
    #define PLATFORM_UNKNOWN 1
    #define PLATFORM_NAME "Unknown"
#endif

namespace jupiter {
namespace platform {

// Forward declarations
class Window;
class FileSystem;
class DynamicLibrary;

// ============================================================================
// Timing and Performance
// ============================================================================

/**
 * @brief High-precision timer for measuring elapsed time
 */
class PLATFORM_API Timer {
public:
    Timer();
    ~Timer() = default;

    /**
     * @brief Reset the timer to start measuring from now
     */
    void reset();

    /**
     * @brief Get elapsed time in seconds since reset
     * @return Elapsed time in seconds
     */
    double getElapsedSeconds() const;

    /**
     * @brief Get elapsed time in milliseconds since reset
     * @return Elapsed time in milliseconds
     */
    double getElapsedMilliseconds() const;

    /**
     * @brief Get elapsed time in microseconds since reset
     * @return Elapsed time in microseconds
     */
    double getElapsedMicroseconds() const;

    /**
     * @brief Get current timestamp in seconds since epoch
     * @return Current timestamp
     */
    static double getCurrentTimestamp();

    /**
     * @brief Sleep for the specified number of milliseconds
     * @param milliseconds Time to sleep
     */
    static void sleep(uint32_t milliseconds);

private:
    uint64_t start_time_;
};

/**
 * @brief High-performance counter for precise timing
 */
class PLATFORM_API PerformanceCounter {
public:
    /**
     * @brief Get the current performance counter value
     * @return Current counter value
     */
    static uint64_t getCurrentValue();

    /**
     * @brief Get the performance counter frequency
     * @return Counter frequency in Hz
     */
    static uint64_t getFrequency();

    /**
     * @brief Convert counter value to seconds
     * @param counter_value Counter value to convert
     * @return Time in seconds
     */
    static double toSeconds(uint64_t counter_value);

    /**
     * @brief Convert counter value to milliseconds
     * @param counter_value Counter value to convert
     * @return Time in milliseconds
     */
    static double toMilliseconds(uint64_t counter_value);
};

// ============================================================================
// Memory Management
// ============================================================================

/**
 * @brief Platform-specific memory allocation functions
 */
class PLATFORM_API Memory {
public:
    /**
     * @brief Allocate aligned memory
     * @param size Size in bytes
     * @param alignment Alignment (must be power of 2)
     * @return Pointer to allocated memory, or nullptr on failure
     */
    static void* allocateAligned(size_t size, size_t alignment);

    /**
     * @brief Free aligned memory
     * @param ptr Pointer to memory to free
     */
    static void freeAligned(void* ptr);

    /**
     * @brief Get cache line size for the current platform
     * @return Cache line size in bytes
     */
    static size_t getCacheLineSize();

    /**
     * @brief Prefetch memory for better performance
     * @param ptr Pointer to memory to prefetch
     * @param offset Offset from ptr
     */
    static void prefetch(const void* ptr, ptrdiff_t offset = 0);
};

// ============================================================================
// Threading (Lock-free primitives)
// ============================================================================

/**
 * @brief Atomic operations for lock-free programming
 */
class PLATFORM_API Atomic {
public:
    /**
     * @brief Atomic load with memory order
     */
    template<typename T>
    static T load(const std::atomic<T>& atomic, std::memory_order order = std::memory_order_seq_cst) {
        return atomic.load(order);
    }

    /**
     * @brief Atomic store with memory order
     */
    template<typename T>
    static void store(std::atomic<T>& atomic, T value, std::memory_order order = std::memory_order_seq_cst) {
        atomic.store(value, order);
    }

    /**
     * @brief Atomic compare and exchange
     */
    template<typename T>
    static bool compareExchange(std::atomic<T>& atomic, T& expected, T desired,
                               std::memory_order order = std::memory_order_seq_cst) {
        return atomic.compare_exchange_strong(expected, desired, order);
    }

    /**
     * @brief Atomic fetch add
     */
    template<typename T>
    static T fetchAdd(std::atomic<T>& atomic, T value, std::memory_order order = std::memory_order_seq_cst) {
        return atomic.fetch_add(value, order);
    }

    /**
     * @brief Atomic fetch subtract
     */
    template<typename T>
    static T fetchSub(std::atomic<T>& atomic, T value, std::memory_order order = std::memory_order_seq_cst) {
        return atomic.fetch_sub(value, order);
    }

    /**
     * @brief Pause for spin-wait loops (platform-specific)
     */
    static void spinWait();
};

/**
 * @brief Thread utilities
 */
class PLATFORM_API Thread {
public:
    /**
     * @brief Get the current thread ID
     * @return Thread ID
     */
    static uint64_t getCurrentThreadId();

    /**
     * @brief Set thread name (for debugging)
     * @param name Thread name
     */
    static void setCurrentThreadName(const char* name);

    /**
     * @brief Get the number of hardware threads available
     * @return Number of hardware threads
     */
    static unsigned int getHardwareConcurrency();

    /**
     * @brief Yield the current thread
     */
    static void yield();
};

// ============================================================================
// File System
// ============================================================================

/**
 * @brief File system utilities
 */
class PLATFORM_API FileSystem {
public:
    /**
     * @brief Check if a file exists
     * @param path File path
     * @return true if file exists
     */
    static bool fileExists(const std::string& path);

    /**
     * @brief Check if a directory exists
     * @param path Directory path
     * @return true if directory exists
     */
    static bool directoryExists(const std::string& path);

    /**
     * @brief Create a directory
     * @param path Directory path
     * @return true on success
     */
    static bool createDirectory(const std::string& path);

    /**
     * @brief Remove a file
     * @param path File path
     * @return true on success
     */
    static bool removeFile(const std::string& path);

    /**
     * @brief Remove a directory
     * @param path Directory path
     * @return true on success
     */
    static bool removeDirectory(const std::string& path);

    /**
     * @brief Get file size
     * @param path File path
     * @return File size in bytes, or -1 on error
     */
    static int64_t getFileSize(const std::string& path);

    /**
     * @brief Get the current working directory
     * @return Current working directory path
     */
    static std::string getCurrentWorkingDirectory();

    /**
     * @brief Set the current working directory
     * @param path Directory path
     * @return true on success
     */
    static bool setCurrentWorkingDirectory(const std::string& path);

    /**
     * @brief Get directory separator character
     * @return Directory separator ('/' or '\\')
     */
    static char getDirectorySeparator();

    /**
     * @brief Normalize path separators to platform-specific format
     * @param path Path to normalize
     * @return Normalized path
     */
    static std::string normalizePath(const std::string& path);

    /**
     * @brief Get absolute path from relative path
     * @param path Relative path
     * @return Absolute path
     */
    static std::string getAbsolutePath(const std::string& path);

    /**
     * @brief List files in directory
     * @param path Directory path
     * @param recursive Include subdirectories
     * @return Vector of file paths
     */
    static std::vector<std::string> listFiles(const std::string& path, bool recursive = false);
};

// ============================================================================
// Dynamic Library Loading
// ============================================================================

/**
 * @brief Dynamic library loader
 */
class PLATFORM_API DynamicLibrary {
public:
    /**
     * @brief Load a dynamic library
     * @param path Library path
     * @return Library instance, or nullptr on failure
     */
    static std::unique_ptr<DynamicLibrary> load(const std::string& path);

    /**
     * @brief Unload the library
     */
    ~DynamicLibrary();

    /**
     * @brief Get function pointer from library
     * @param functionName Function name
     * @return Function pointer, or nullptr if not found
     */
    void* getFunction(const char* functionName);

    /**
     * @brief Check if library is loaded
     * @return true if loaded
     */
    bool isLoaded() const;

    /**
     * @brief Get library path
     * @return Library path
     */
    const std::string& getPath() const;

private:
    DynamicLibrary(void* handle, const std::string& path);
    void* handle_;
    std::string path_;
};

// ============================================================================
// System Information
// ============================================================================

/**
 * @brief System information utilities
 */
class PLATFORM_API SystemInfo {
public:
    /**
     * @brief Get platform name
     * @return Platform name string
     */
    static const char* getPlatformName();

    /**
     * @brief Get total system memory in bytes
     * @return Total memory in bytes
     */
    static uint64_t getTotalMemoryBytes();

    /**
     * @brief Get available system memory in bytes
     * @return Available memory in bytes
     */
    static uint64_t getAvailableMemoryBytes();

    /**
     * @brief Get page size
     * @return Page size in bytes
     */
    static size_t getPageSize();

    /**
     * @brief Get CPU architecture string
     * @return CPU architecture
     */
    static std::string getCpuArchitecture();

    /**
     * @brief Check if running on battery power
     * @return true if on battery
     */
    static bool isOnBattery();

    /**
     * @brief Get system language
     * @return Language code (e.g., "en", "fr", "de")
     */
    static std::string getSystemLanguage();
};

// ============================================================================
// Window and Input (Basic interfaces - to be extended)
// ============================================================================

/**
 * @brief Basic window interface
 */
class PLATFORM_API Window {
public:
    struct CreateParams {
        std::string title;
        int width;
        int height;
        int x;
        int y;
        bool resizable;
        bool fullscreen;
    };

    /**
     * @brief Create a window
     * @param params Window creation parameters
     * @return Window instance, or nullptr on failure
     */
    static std::unique_ptr<Window> create(const CreateParams& params);

    virtual ~Window() = default;

    /**
     * @brief Process window messages/events
     * @return true if window should continue running
     */
    virtual bool processMessages() = 0;

    /**
     * @brief Get window width
     * @return Window width
     */
    virtual int getWidth() const = 0;

    /**
     * @brief Get window height
     * @return Window height
     */
    virtual int getHeight() const = 0;

    /**
     * @brief Set window title
     * @param title New title
     */
    virtual void setTitle(const std::string& title) = 0;

    /**
     * @brief Check if window should close
     * @return true if window should close
     */
    virtual bool shouldClose() const = 0;

    /**
     * @brief Show the window
     */
    virtual void show() = 0;

    /**
     * @brief Hide the window
     */
    virtual void hide() = 0;
};

/**
 * @brief Initialize the platform abstraction layer
 * @return true if initialization was successful, false otherwise
 */
PLATFORM_API bool initialize();

/**
 * @brief Shutdown the platform abstraction layer
 */
PLATFORM_API void shutdown();

} // namespace platform
} // namespace jupiter
