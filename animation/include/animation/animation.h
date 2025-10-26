#pragma once

#ifdef _WIN32
    #ifdef ANIMATION_EXPORTS
        #define ANIMATION_API __declspec(dllexport)
    #elif defined(ANIMATION_IMPORTS)
        #define ANIMATION_API __declspec(dllimport)
    #else
        #define ANIMATION_API
    #endif
#else
    #define ANIMATION_API
#endif

namespace jupiter {
namespace animation {

/**
 * @brief Initialize the animation subsystem
 * @return true if initialization was successful, false otherwise
 */
ANIMATION_API bool initialize();

/**
 * @brief Shutdown the animation subsystem
 */
ANIMATION_API void shutdown();

/**
 * @brief Update all animations
 * @param deltaTime Time elapsed since last update in seconds
 */
ANIMATION_API void update(float deltaTime);

} // namespace animation
} // namespace jupiter
