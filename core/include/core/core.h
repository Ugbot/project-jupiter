#pragma once

#ifdef _WIN32
    #ifdef CORE_EXPORTS
        #define CORE_API __declspec(dllexport)
    #elif defined(CORE_IMPORTS)
        #define CORE_API __declspec(dllimport)
    #else
        #define CORE_API
    #endif
#else
    #define CORE_API
#endif

namespace jupiter {
namespace core {

/**
 * @brief Initialize the core subsystem
 * @return true if initialization was successful, false otherwise
 */
CORE_API bool initialize();

/**
 * @brief Shutdown the core subsystem
 */
CORE_API void shutdown();

/**
 * @brief Get the version string of the core subsystem
 * @return Version string
 */
CORE_API const char* getVersion();

} // namespace core
} // namespace jupiter
