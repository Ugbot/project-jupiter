#pragma once

#include <atomic>
#include <cstdint>

// Forward declare SDL types to avoid including SDL headers in public headers
struct SDL_Window;

namespace jupiter {
namespace platform {
namespace sdl {

/**
 * @brief RAII-based SDL initialization and shutdown manager
 *
 * This class provides thread-safe, lock-free initialization and shutdown
 * of SDL subsystems. It follows the engine's principles of no runtime
 * allocations and lock-free design where possible.
 *
 * Usage:
 *   SDLWrapper sdl;  // Initializes SDL
 *   // ... use SDL ...
 *   // SDL automatically shut down when sdl goes out of scope
 */
class SDLWrapper {
public:
    /**
     * @brief Initialize SDL subsystems
     * @param initVideo Initialize video subsystem (default: true)
     * @param initAudio Initialize audio subsystem (default: true)
     * @param initEvents Initialize event subsystem (default: true)
     */
    SDLWrapper(bool initVideo = true, bool initAudio = true, bool initEvents = true);

    /**
     * @brief Shutdown SDL subsystems
     */
    ~SDLWrapper();

    // Non-copyable, non-movable (RAII pattern)
    SDLWrapper(const SDLWrapper&) = delete;
    SDLWrapper& operator=(const SDLWrapper&) = delete;
    SDLWrapper(SDLWrapper&&) = delete;
    SDLWrapper& operator=(SDLWrapper&&) = delete;

    /**
     * @brief Check if SDL was initialized successfully
     * @return true if SDL is initialized
     */
    bool isInitialized() const noexcept;

    /**
     * @brief Get the last SDL error message
     * @return Error message string, or empty if no error
     */
    const char* getError() const noexcept;

    /**
     * @brief Poll SDL events (non-blocking)
     *
     * This should be called once per frame to process window events,
     * input events, etc. Events are placed into lock-free queues for
     * processing by other systems.
     *
     * @return true if application should continue running
     */
    bool pollEvents() noexcept;

    /**
     * @brief Check if quit has been requested (e.g., window close button)
     * @return true if quit was requested
     */
    bool shouldQuit() const noexcept;

private:
    // Atomic flag for thread-safe initialization tracking
    std::atomic<bool> initialized_;

    // Atomic flag for quit request
    std::atomic<bool> quit_requested_;

    // Track which subsystems were initialized
    uint32_t initialized_subsystems_;
};

/**
 * @brief Get the global SDL initialization state
 *
 * This is a lock-free query to check if SDL has been initialized
 * by any SDLWrapper instance in the process.
 *
 * @return true if SDL is currently initialized
 */
bool isSDLInitialized() noexcept;

} // namespace sdl
} // namespace platform
} // namespace jupiter
