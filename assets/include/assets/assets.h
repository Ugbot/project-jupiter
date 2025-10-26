#pragma once

#ifdef _WIN32
    #ifdef ASSETS_EXPORTS
        #define ASSETS_API __declspec(dllexport)
    #elif defined(ASSETS_IMPORTS)
        #define ASSETS_API __declspec(dllimport)
    #else
        #define ASSETS_API
    #endif
#else
    #define ASSETS_API
#endif

namespace jupiter {
namespace assets {

/**
 * @brief Initialize the assets subsystem
 * @return true if initialization was successful, false otherwise
 */
ASSETS_API bool initialize();

/**
 * @brief Shutdown the assets subsystem
 */
ASSETS_API void shutdown();

/**
 * @brief Load an asset from file
 * @param filename Path to the asset file
 * @return true if asset was loaded successfully, false otherwise
 */
ASSETS_API bool loadAsset(const char* filename);

} // namespace assets
} // namespace jupiter
