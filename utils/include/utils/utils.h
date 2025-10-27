#pragma once

#ifdef _WIN32
    #ifdef UTILS_EXPORTS
        #define UTILS_API __declspec(dllexport)
    #elif defined(UTILS_IMPORTS)
        #define UTILS_API __declspec(dllimport)
    #else
        #define UTILS_API
    #endif
#else
    #define UTILS_API
#endif

#include <string>
#include <vector>
#include <chrono>
#include <functional>

namespace jupiter {
namespace utils {

/**
 * @brief String utilities
 */
class UTILS_API StringUtils {
public:
    /**
     * @brief Split a string by delimiter
     * @param str String to split
     * @param delimiter Delimiter character
     * @return Vector of string segments
     */
    static std::vector<std::string> split(const std::string& str, char delimiter);

    /**
     * @brief Join strings with delimiter
     * @param strings Vector of strings to join
     * @param delimiter Delimiter string
     * @return Joined string
     */
    static std::string join(const std::vector<std::string>& strings, const std::string& delimiter);

    /**
     * @brief Trim whitespace from both ends of string
     * @param str String to trim
     * @return Trimmed string
     */
    static std::string trim(const std::string& str);

    /**
     * @brief Convert string to lowercase
     * @param str String to convert
     * @return Lowercase string
     */
    static std::string toLower(const std::string& str);

    /**
     * @brief Convert string to uppercase
     * @param str String to convert
     * @return Uppercase string
     */
    static std::string toUpper(const std::string& str);

    /**
     * @brief Check if string starts with prefix
     * @param str String to check
     * @param prefix Prefix to check for
     * @return true if string starts with prefix
     */
    static bool startsWith(const std::string& str, const std::string& prefix);

    /**
     * @brief Check if string ends with suffix
     * @param str String to check
     * @param suffix Suffix to check for
     * @return true if string ends with suffix
     */
    static bool endsWith(const std::string& str, const std::string& suffix);

    /**
     * @brief Replace all occurrences of a substring
     * @param str String to modify
     * @param from Substring to replace
     * @param to Replacement substring
     * @return Modified string
     */
    static std::string replace(const std::string& str, const std::string& from, const std::string& to);
};

/**
 * @brief File utilities
 */
class UTILS_API FileUtils {
public:
    /**
     * @brief Check if file exists
     * @param path File path
     * @return true if file exists
     */
    static bool exists(const std::string& path);

    /**
     * @brief Get file size in bytes
     * @param path File path
     * @return File size, or -1 on error
     */
    static long long getFileSize(const std::string& path);

    /**
     * @brief Read entire file as string
     * @param path File path
     * @return File contents as string
     */
    static std::string readFile(const std::string& path);

    /**
     * @brief Write string to file
     * @param path File path
     * @param content Content to write
     * @return true on success
     */
    static bool writeFile(const std::string& path, const std::string& content);

    /**
     * @brief Get file extension
     * @param path File path
     * @return File extension (without dot)
     */
    static std::string getExtension(const std::string& path);

    /**
     * @brief Get filename without path
     * @param path File path
     * @return Filename
     */
    static std::string getFilename(const std::string& path);

    /**
     * @brief Get directory path from file path
     * @param path File path
     * @return Directory path
     */
    static std::string getDirectory(const std::string& path);
};

/**
 * @brief Timer utilities
 */
class UTILS_API Timer {
public:
    Timer();

    /**
     * @brief Reset the timer
     */
    void reset();

    /**
     * @brief Get elapsed time in seconds
     * @return Elapsed time
     */
    double getElapsedSeconds() const;

    /**
     * @brief Get elapsed time in milliseconds
     * @return Elapsed time
     */
    double getElapsedMilliseconds() const;

    /**
     * @brief Get elapsed time in microseconds
     * @return Elapsed time
     */
    double getElapsedMicroseconds() const;

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
};

/**
 * @brief Scoped timer for measuring execution time
 */
class UTILS_API ScopedTimer {
public:
    ScopedTimer(const std::string& name);
    ~ScopedTimer();

private:
    std::string m_name;
    Timer m_timer;
};

/**
 * @brief Hash utilities
 */
class UTILS_API HashUtils {
public:
    /**
     * @brief Simple string hash function (FNV-1a)
     * @param str String to hash
     * @return 32-bit hash value
     */
    static uint32_t fnv1a32(const std::string& str);

    /**
     * @brief Simple string hash function (FNV-1a)
     * @param str String to hash
     * @return 64-bit hash value
     */
    static uint64_t fnv1a64(const std::string& str);
};

/**
 * @brief Random utilities
 */
class UTILS_API RandomUtils {
public:
    /**
     * @brief Generate random integer in range [min, max]
     * @param min Minimum value (inclusive)
     * @param max Maximum value (inclusive)
     * @return Random integer
     */
    static int randomInt(int min, int max);

    /**
     * @brief Generate random float in range [min, max)
     * @param min Minimum value (inclusive)
     * @param max Maximum value (exclusive)
     * @return Random float
     */
    static float randomFloat(float min, float max);

    /**
     * @brief Generate random double in range [min, max)
     * @param min Minimum value (inclusive)
     * @param max Maximum value (exclusive)
     * @return Random double
     */
    static double randomDouble(double min, double max);

    /**
     * @brief Generate random boolean
     * @return Random boolean
     */
    static bool randomBool();

    /**
     * @brief Set random seed
     * @param seed Random seed
     */
    static void setSeed(unsigned int seed);
};

/**
 * @brief Initialize the utils subsystem
 * @return true if initialization was successful, false otherwise
 */
UTILS_API bool initialize();

/**
 * @brief Shutdown the utils subsystem
 */
UTILS_API void shutdown();

} // namespace utils
} // namespace jupiter
