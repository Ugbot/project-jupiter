#pragma once

#ifdef _WIN32
    #ifdef AUDIO_EXPORTS
        #define AUDIO_API __declspec(dllexport)
    #elif defined(AUDIO_IMPORTS)
        #define AUDIO_API __declspec(dllimport)
    #else
        #define AUDIO_API
    #endif
#else
    #define AUDIO_API
#endif

namespace jupiter {
namespace audio {

/**
 * @brief Initialize the audio subsystem
 * @return true if initialization was successful, false otherwise
 */
AUDIO_API bool initialize();

/**
 * @brief Shutdown the audio subsystem
 */
AUDIO_API void shutdown();

/**
 * @brief Play a sound
 * @param soundName Name of the sound to play
 */
AUDIO_API void playSound(const char* soundName);

} // namespace audio
} // namespace jupiter
