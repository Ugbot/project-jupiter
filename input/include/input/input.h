#pragma once

#ifdef _WIN32
    #ifdef INPUT_EXPORTS
        #define INPUT_API __declspec(dllexport)
    #elif defined(INPUT_IMPORTS)
        #define INPUT_API __declspec(dllimport)
    #else
        #define INPUT_API
    #endif
#else
    #define INPUT_API
#endif

namespace jupiter {
namespace input {

/**
 * @brief Initialize the input subsystem
 * @return true if initialization was successful, false otherwise
 */
INPUT_API bool initialize();

/**
 * @brief Shutdown the input subsystem
 */
INPUT_API void shutdown();

/**
 * @brief Update input state (poll for events)
 */
INPUT_API void update();

/**
 * @brief Check if a key is pressed
 * @param keyCode Key code to check
 * @return true if key is pressed, false otherwise
 */
INPUT_API bool isKeyPressed(int keyCode);

} // namespace input
} // namespace jupiter
