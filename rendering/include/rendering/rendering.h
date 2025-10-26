#pragma once

#ifdef _WIN32
    #ifdef RENDERING_EXPORTS
        #define RENDERING_API __declspec(dllexport)
    #elif defined(RENDERING_IMPORTS)
        #define RENDERING_API __declspec(dllimport)
    #else
        #define RENDERING_API
    #endif
#else
    #define RENDERING_API
#endif

namespace jupiter {
namespace rendering {

/**
 * @brief Initialize the rendering subsystem
 * @return true if initialization was successful, false otherwise
 */
RENDERING_API bool initialize();

/**
 * @brief Shutdown the rendering subsystem
 */
RENDERING_API void shutdown();

/**
 * @brief Render a frame
 */
RENDERING_API void renderFrame();

/**
 * @brief Clear the screen
 */
RENDERING_API void clear();

} // namespace rendering
} // namespace jupiter
