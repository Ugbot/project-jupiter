#include "platform/sdl_wrapper.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace jupiter {
namespace platform {
namespace sdl {

// Global atomic flag to track SDL initialization state across instances
static std::atomic<int> g_sdl_init_count{0};

SDLWrapper::SDLWrapper(bool initVideo, bool initAudio, bool initEvents)
    : initialized_(false)
    , quit_requested_(false)
    , initialized_subsystems_(0)
{
    // Build SDL initialization flags based on requested subsystems
    Uint32 flags = 0;

    if (initVideo) {
        flags |= SDL_INIT_VIDEO;
    }
    if (initAudio) {
        flags |= SDL_INIT_AUDIO;
    }
    if (initEvents) {
        flags |= SDL_INIT_EVENTS;
    }

    // Initialize SDL with requested subsystems
    // SDL3 returns true on success, false on failure
    if (SDL_Init(flags)) {
        initialized_.store(true, std::memory_order_release);
        initialized_subsystems_ = flags;

        // Atomically increment global init count
        g_sdl_init_count.fetch_add(1, std::memory_order_release);

        std::cout << "[SDL] Initialized successfully" << std::endl;
        if (initVideo) std::cout << "  -> Video subsystem enabled" << std::endl;
        if (initAudio) std::cout << "  -> Audio subsystem enabled" << std::endl;
        if (initEvents) std::cout << "  -> Events subsystem enabled" << std::endl;
    } else {
        std::cerr << "[SDL] Initialization failed: " << SDL_GetError() << std::endl;
        initialized_.store(false, std::memory_order_release);
    }
}

SDLWrapper::~SDLWrapper() {
    if (initialized_.load(std::memory_order_acquire)) {
        // Quit SDL subsystems
        SDL_Quit();

        // Atomically decrement global init count
        g_sdl_init_count.fetch_sub(1, std::memory_order_release);

        std::cout << "[SDL] Shutdown complete" << std::endl;
    }
}

bool SDLWrapper::isInitialized() const noexcept {
    return initialized_.load(std::memory_order_acquire);
}

const char* SDLWrapper::getError() const noexcept {
    return SDL_GetError();
}

bool SDLWrapper::pollEvents() noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                quit_requested_.store(true, std::memory_order_release);
                return false;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                quit_requested_.store(true, std::memory_order_release);
                return false;

            // TODO: Add more event handlers here as needed
            // - SDL_EVENT_KEY_DOWN / SDL_EVENT_KEY_UP for keyboard input
            // - SDL_EVENT_MOUSE_MOTION / SDL_EVENT_MOUSE_BUTTON_* for mouse input
            // - SDL_EVENT_WINDOW_RESIZED for window resize
            // - etc.

            default:
                break;
        }
    }

    return !quit_requested_.load(std::memory_order_acquire);
}

bool SDLWrapper::shouldQuit() const noexcept {
    return quit_requested_.load(std::memory_order_acquire);
}

bool isSDLInitialized() noexcept {
    return g_sdl_init_count.load(std::memory_order_acquire) > 0;
}

} // namespace sdl
} // namespace platform
} // namespace jupiter
