# SDL Integration Migration Guide for Project Jupiter

## Executive Summary

This guide details the migration from GLFW to SDL3 (Simple DirectMedia Layer) to provide unified platform abstraction for windowing, audio, input, and threading in the Project Jupiter game engine. SDL will become the single platform abstraction layer, replacing GLFW and implementing stubs in platform/audio/input modules.

## Why SDL3?

**Benefits:**
- **Single Library**: Replaces GLFW + platform-specific code with one battle-tested library
- **Comprehensive**: Windowing, audio, input (keyboard/mouse/gamepad), threading, timers, file I/O
- **Vulkan Support**: Native SDL_Vulkan_* functions for surface creation
- **Cross-Platform**: Windows, macOS, Linux, mobile (iOS/Android), consoles
- **Audio**: SDL_mixer for sound effects and music with minimal latency
- **Performance**: Lock-free queues for events, optimized for games

**Alignment with CLAUDE.md Principles:**
- Lock-free event queue (SDL's event system)
- Pre-allocated audio buffers (SDL_AudioStream with ring buffers)
- Platform-agnostic by design
- C++ wrapper can enforce data-oriented design
- Event sourcing model maps naturally to SDL events

---

## Phase 1: Add SDL3 as Vendored Dependency

### Step 1.1: Download and Vendor SDL3

**Actions:**
```bash
cd /Users/bengamble/project-jupiter/vendored
git clone https://github.com/libsdl-org/SDL.git sdl3
cd sdl3
git checkout main  # SDL3 is on main branch
```

**File:** `vendored/README.md`
```markdown
# Vendored Dependencies

## SDL3 (Simple DirectMedia Layer)
- **Version**: 3.x (main branch)
- **Purpose**: Platform abstraction (windowing, audio, input, threading)
- **License**: zlib License
- **URL**: https://github.com/libsdl-org/SDL
- **Replaced**: GLFW (windowing only)

## GLFW (OpenGL FrameWork) - DEPRECATED, will be removed
- **Version**: 3.x
- **Purpose**: Windowing (being replaced by SDL3)
- **License**: zlib License
- **Status**: To be removed after SDL migration complete

## Vulkan SDK
- **Purpose**: Graphics API headers and validation layers
- **Status**: Kept, used alongside SDL3
```

### Step 1.2: Create SDL CMake Integration

**File:** `vendored/CMakeLists.txt` (CREATE NEW)
```cmake
# Vendored Dependencies Build Configuration

# SDL3 Configuration
message(STATUS "Configuring SDL3...")
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(sdl3)

message(STATUS "SDL3 configured successfully")
```

**File:** `CMakeLists.txt` (UPDATE root CMakeLists.txt)

Add this line before the core libraries:
```cmake
# Add vendored dependencies
add_subdirectory(vendored)
```

---

## Phase 2: Create SDL Platform Wrapper Layer

### Step 2.1: Update Platform Module Structure

**Directory Structure:**
```
platform/
├── CMakeLists.txt
├── include/platform/
│   ├── platform.h           # Main header (existing)
│   ├── sdl_wrapper.h        # NEW: SDL C++ wrapper
│   ├── window.h             # NEW: Window abstraction
│   ├── input.h              # NEW: Input abstraction
│   └── audio_device.h       # NEW: Audio device abstraction
└── src/
    ├── platform.cpp         # Existing implementations
    ├── sdl_wrapper.cpp      # NEW: SDL initialization/shutdown
    ├── window_sdl.cpp       # NEW: SDL window implementation
    ├── input_sdl.cpp        # NEW: SDL input implementation
    └── audio_device_sdl.cpp # NEW: SDL audio implementation
```

### Step 2.2: Implement SDL Wrapper

**File:** `platform/include/platform/sdl_wrapper.h` (CREATE)
```cpp
#pragma once

#include "platform.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

namespace jupiter {
namespace platform {
namespace sdl {

/**
 * @brief Initialize SDL subsystems
 *
 * Call once at engine startup. Initializes VIDEO, AUDIO, EVENTS, TIMER.
 * Thread-safe, idempotent.
 *
 * @return true if successful
 */
PLATFORM_API bool initialize();

/**
 * @brief Shutdown SDL subsystems
 *
 * Call once at engine shutdown. Cleans up all SDL resources.
 */
PLATFORM_API void shutdown();

/**
 * @brief Check if SDL is initialized
 */
PLATFORM_API bool isInitialized();

/**
 * @brief Get SDL version string
 */
PLATFORM_API std::string getVersion();

/**
 * @brief Poll SDL events (should be called once per frame)
 *
 * This pumps the SDL event queue and dispatches to our event system.
 * Must be called on the main thread.
 */
PLATFORM_API void pollEvents();

} // namespace sdl
} // namespace platform
} // namespace jupiter
```

**File:** `platform/src/sdl_wrapper.cpp` (CREATE)
```cpp
#include "platform/sdl_wrapper.h"
#include "logging/logging.h"
#include <atomic>

namespace jupiter {
namespace platform {
namespace sdl {

namespace {
    std::atomic<bool> initialized_{false};
}

bool initialize() {
    if (initialized_.load(std::memory_order_acquire)) {
        LOG_INFO("SDL", "SDL already initialized");
        return true;
    }

    // Initialize SDL with VIDEO, AUDIO, EVENTS, TIMER
    Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER;

    if (SDL_Init(flags) != 0) {
        LOG_ERROR("SDL", "Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }

    // Set hints for Vulkan support
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");

    initialized_.store(true, std::memory_order_release);

    LOG_INFO("SDL", "SDL initialized successfully (version %d.%d.%d)",
             SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);

    return true;
}

void shutdown() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return;
    }

    SDL_Quit();
    initialized_.store(false, std::memory_order_release);

    LOG_INFO("SDL", "SDL shutdown complete");
}

bool isInitialized() {
    return initialized_.load(std::memory_order_acquire);
}

std::string getVersion() {
    return std::to_string(SDL_MAJOR_VERSION) + "." +
           std::to_string(SDL_MINOR_VERSION) + "." +
           std::to_string(SDL_PATCHLEVEL);
}

void pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Events will be dispatched to event_system module
        // For now, just consume them
        // TODO: Dispatch to event_system::EventQueue

        // Handle quit event
        if (event.type == SDL_EVENT_QUIT) {
            // Will be handled by window shouldClose check
        }
    }
}

} // namespace sdl
} // namespace platform
} // namespace jupiter
```

### Step 2.3: Implement SDL Window Wrapper

**File:** `platform/include/platform/window.h` (CREATE)
```cpp
#pragma once

#include "platform.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>
#include <vector>

namespace jupiter {
namespace platform {

/**
 * @brief Platform-agnostic window abstraction using SDL3
 *
 * Manages window creation, Vulkan surface creation, and events.
 * Designed for zero runtime allocation after initialization.
 */
class PLATFORM_API SDLWindow {
public:
    struct Config {
        std::string title = "Jupiter Engine";
        uint32_t width = 800;
        uint32_t height = 600;
        bool resizable = false;
        bool fullscreen = false;
        bool vulkan = true;  // Enable Vulkan support
    };

    SDLWindow();
    ~SDLWindow();

    // No copy, allow move
    SDLWindow(const SDLWindow&) = delete;
    SDLWindow& operator=(const SDLWindow&) = delete;
    SDLWindow(SDLWindow&&) noexcept;
    SDLWindow& operator=(SDLWindow&&) noexcept;

    /**
     * @brief Create window with given configuration
     *
     * Pre-allocates all resources. No allocations during window lifetime.
     */
    bool create(const Config& config);

    /**
     * @brief Destroy window and free resources
     */
    void destroy();

    /**
     * @brief Check if window should close (user clicked X)
     */
    bool shouldClose() const;

    /**
     * @brief Get window width
     */
    uint32_t getWidth() const;

    /**
     * @brief Get window height
     */
    uint32_t getHeight() const;

    /**
     * @brief Create Vulkan surface (for rendering module)
     *
     * @param instance Vulkan instance
     * @param surface Output surface handle
     * @return true if successful
     */
    bool createVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const;

    /**
     * @brief Get required Vulkan instance extensions
     *
     * Returns pre-allocated array of extension names.
     */
    std::vector<const char*> getRequiredVulkanExtensions() const;

    /**
     * @brief Get native SDL window handle (for advanced use)
     */
    SDL_Window* getNativeHandle() const { return window_; }

private:
    SDL_Window* window_;
    Config config_;
    bool shouldClose_;
};

} // namespace platform
} // namespace jupiter
```

**File:** `platform/src/window_sdl.cpp` (CREATE)
```cpp
#include "platform/window.h"
#include "platform/sdl_wrapper.h"
#include "logging/logging.h"
#include <SDL3/SDL_vulkan.h>

namespace jupiter {
namespace platform {

SDLWindow::SDLWindow()
    : window_(nullptr)
    , shouldClose_(false) {
}

SDLWindow::~SDLWindow() {
    destroy();
}

SDLWindow::SDLWindow(SDLWindow&& other) noexcept
    : window_(other.window_)
    , config_(std::move(other.config_))
    , shouldClose_(other.shouldClose_) {
    other.window_ = nullptr;
    other.shouldClose_ = false;
}

SDLWindow& SDLWindow::operator=(SDLWindow&& other) noexcept {
    if (this != &other) {
        destroy();

        window_ = other.window_;
        config_ = std::move(other.config_);
        shouldClose_ = other.shouldClose_;

        other.window_ = nullptr;
        other.shouldClose_ = false;
    }
    return *this;
}

bool SDLWindow::create(const Config& config) {
    config_ = config;

    // Ensure SDL is initialized
    if (!sdl::isInitialized()) {
        LOG_ERROR("SDLWindow", "SDL not initialized, call platform::sdl::initialize() first");
        return false;
    }

    // Build window flags
    Uint32 flags = 0;

    if (config.vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    }

    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Create window
    window_ = SDL_CreateWindow(
        config.title.c_str(),
        config.width,
        config.height,
        flags
    );

    if (!window_) {
        LOG_ERROR("SDLWindow", "Failed to create SDL window: %s", SDL_GetError());
        return false;
    }

    shouldClose_ = false;

    LOG_INFO("SDLWindow", "Created window '%s' (%dx%d)",
             config.title.c_str(), config.width, config.height);

    return true;
}

void SDLWindow::destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        LOG_INFO("SDLWindow", "Window destroyed");
    }
}

bool SDLWindow::shouldClose() const {
    // Check for SDL quit events
    // This is handled in sdl::pollEvents(), we just track the flag
    return shouldClose_;
}

uint32_t SDLWindow::getWidth() const {
    if (!window_) return 0;

    int w, h;
    SDL_GetWindowSize(window_, &w, &h);
    return static_cast<uint32_t>(w);
}

uint32_t SDLWindow::getHeight() const {
    if (!window_) return 0;

    int w, h;
    SDL_GetWindowSize(window_, &w, &h);
    return static_cast<uint32_t>(h);
}

bool SDLWindow::createVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    if (!window_) {
        LOG_ERROR("SDLWindow", "Cannot create Vulkan surface: window not created");
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, surface)) {
        LOG_ERROR("SDLWindow", "Failed to create Vulkan surface: %s", SDL_GetError());
        return false;
    }

    LOG_INFO("SDLWindow", "Created Vulkan surface");
    return true;
}

std::vector<const char*> SDLWindow::getRequiredVulkanExtensions() const {
    if (!window_) {
        LOG_ERROR("SDLWindow", "Cannot get Vulkan extensions: window not created");
        return {};
    }

    unsigned int count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);

    if (!extensions) {
        LOG_ERROR("SDLWindow", "Failed to get Vulkan extensions: %s", SDL_GetError());
        return {};
    }

    std::vector<const char*> result;
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        result.push_back(extensions[i]);
    }

    LOG_INFO("SDLWindow", "Required Vulkan extensions: %u", count);
    return result;
}

} // namespace platform
} // namespace jupiter
```

---

## Phase 3: Migrate Rendering Module from GLFW to SDL

### Step 3.1: Update Rendering Module Dependencies

**File:** `rendering/CMakeLists.txt` (MODIFY)

Replace GLFW configuration:
```cmake
# REMOVE GLFW build
# set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
# set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glfw glfw_build)

add_library(rendering
    src/rendering.cpp
    src/vulkan_backend.cpp
    src/application.cpp
)

target_include_directories(rendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # Include vendored Vulkan headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/base>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external/vulkan>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external/ktx/include>
        # REMOVE GLFW include
        # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glfw/include>
)

# Link against platform abstraction (which now includes SDL)
target_link_libraries(rendering
    PUBLIC
        platform      # Now includes SDL3
        math
        logging
        # REMOVE glfw
        # glfw
)
```

### Step 3.2: Update Rendering Window Class

**File:** `rendering/include/rendering/rendering.h` (MODIFY)

Replace GLFW includes:
```cpp
// REMOVE:
// // GLFW for windowing
// #define GLFW_INCLUDE_VULKAN
// #include <GLFW/glfw3.h>

// ADD:
#include <vulkan/vulkan.h>

// Forward declare instead of including SDL directly
namespace jupiter {
namespace platform {
    class SDLWindow;
}
}
```

Keep Window class as-is, implementation will change internally.

**File:** `rendering/src/rendering.cpp` (MODIFY)

Replace implementation:
```cpp
#include "rendering/rendering.h"
#include "platform/sdl_wrapper.h"
#include "platform/window.h"
#include "logging/logging.h"

// REMOVE: #include <GLFW/glfw3.h>

namespace jupiter {
namespace rendering {

Window::Window() : m_window(nullptr) {}

Window::~Window() {
    shutdown();
}

bool Window::initialize(const WindowConfig& config) {
    m_config = config;

    // Ensure SDL is initialized
    if (!platform::sdl::initialize()) {
        LOG_ERROR("Rendering", "Failed to initialize SDL");
        return false;
    }

    // Create SDL window (wrapped in platform::SDLWindow)
    auto sdlWindow = new platform::SDLWindow();

    platform::SDLWindow::Config sdlConfig;
    sdlConfig.title = config.title;
    sdlConfig.width = config.width;
    sdlConfig.height = config.height;
    sdlConfig.resizable = config.resizable;
    sdlConfig.vulkan = true;

    if (!sdlWindow->create(sdlConfig)) {
        LOG_ERROR("Rendering", "Failed to create SDL window");
        delete sdlWindow;
        return false;
    }

    m_window = reinterpret_cast<GLFWwindow*>(sdlWindow);  // Store as opaque pointer
    return true;
}

void Window::shutdown() {
    if (m_window) {
        auto sdlWindow = reinterpret_cast<platform::SDLWindow*>(m_window);
        sdlWindow->destroy();
        delete sdlWindow;
        m_window = nullptr;
    }
}

bool Window::shouldClose() const {
    if (!m_window) return true;
    auto sdlWindow = reinterpret_cast<platform::SDLWindow*>(m_window);
    return sdlWindow->shouldClose();
}

void Window::pollEvents() {
    platform::sdl::pollEvents();
}

uint32_t Window::getWidth() const {
    if (!m_window) return 0;
    auto sdlWindow = reinterpret_cast<platform::SDLWindow*>(m_window);
    return sdlWindow->getWidth();
}

uint32_t Window::getHeight() const {
    if (!m_window) return 0;
    auto sdlWindow = reinterpret_cast<platform::SDLWindow*>(m_window);
    return sdlWindow->getHeight();
}

VkResult Window::createVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    if (!m_window) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto sdlWindow = reinterpret_cast<platform::SDLWindow*>(m_window);
    bool success = sdlWindow->createVulkanSurface(instance, surface);
    return success ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

std::vector<const char*> Window::getRequiredExtensions() const {
    if (!m_window) {
        return {};
    }

    auto sdlWindow = reinterpret_cast<platform::SDLWindow*>(m_window);
    return sdlWindow->getRequiredVulkanExtensions();
}

// ... rest of rendering.cpp unchanged ...
```

### Step 3.3: Update Application Framework

**File:** `rendering/src/application.cpp` (MODIFY timing)
```cpp
#include "rendering/application.h"
#include "vulkan_backend.h"
#include "logging/logging.h"
#include "platform/platform.h"
#include "platform/sdl_wrapper.h"

// REMOVE: #include <GLFW/glfw3.h>
#include <SDL3/SDL.h>

// ... existing code ...

double Application::getCurrentTime() const {
    // Use SDL timing instead of GLFW
    // SDL_GetTicks64() returns milliseconds since SDL_Init()
    return SDL_GetTicks() / 1000.0;
}
```

---

## Phase 4: Implement Audio System with SDL

### Step 4.1: Design Lock-Free Audio Architecture

**File:** `audio/include/audio/audio.h` (REWRITE)
```cpp
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

#include <cstdint>
#include <string>
#include <atomic>
#include <array>

namespace jupiter {
namespace audio {

// Forward declarations
class AudioDevice;
class AudioSource;
class AudioMixer;

/**
 * @brief Audio system configuration
 */
struct AudioConfig {
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;  // Stereo
    uint32_t bufferSize = 2048;  // Samples per buffer
    uint32_t maxSources = 64;  // Pre-allocate 64 audio sources
};

/**
 * @brief Audio source handle (index into pre-allocated array)
 */
using AudioSourceHandle = uint32_t;
constexpr AudioSourceHandle INVALID_AUDIO_SOURCE = UINT32_MAX;

/**
 * @brief Audio command for lock-free queue
 */
struct AudioCommand {
    enum class Type : uint8_t {
        PLAY,
        STOP,
        SET_VOLUME,
        SET_LOOP
    };

    Type type;
    AudioSourceHandle source;
    float value;  // volume or loop count
    uint64_t timestamp;
};

/**
 * @brief Initialize audio system with SDL
 *
 * Pre-allocates all audio buffers and sources.
 * Call once at engine startup.
 */
AUDIO_API bool initialize(const AudioConfig& config = AudioConfig());

/**
 * @brief Shutdown audio system
 */
AUDIO_API void shutdown();

/**
 * @brief Load audio file and return handle
 *
 * Loads WAV/OGG/MP3 into pre-allocated source slot.
 * Returns INVALID_AUDIO_SOURCE if no slots available or load fails.
 *
 * @param path Path to audio file
 * @return Audio source handle
 */
AUDIO_API AudioSourceHandle loadSound(const char* path);

/**
 * @brief Unload audio source
 */
AUDIO_API void unloadSound(AudioSourceHandle handle);

/**
 * @brief Play audio source
 *
 * Submits PLAY command to lock-free queue.
 * Actual playback starts on audio thread.
 */
AUDIO_API void play(AudioSourceHandle handle, bool loop = false);

/**
 * @brief Stop audio source
 */
AUDIO_API void stop(AudioSourceHandle handle);

/**
 * @brief Set volume (0.0 = silent, 1.0 = full)
 */
AUDIO_API void setVolume(AudioSourceHandle handle, float volume);

/**
 * @brief Get current playback position in seconds
 */
AUDIO_API float getPosition(AudioSourceHandle handle);

/**
 * @brief Check if source is currently playing
 */
AUDIO_API bool isPlaying(AudioSourceHandle handle);

/**
 * @brief Update audio system (call once per frame)
 *
 * Processes pending commands from lock-free queue.
 */
AUDIO_API void update();

/**
 * @brief Legacy API for backward compatibility
 */
inline void playSound(const char* soundName) {
    auto handle = loadSound(soundName);
    if (handle != INVALID_AUDIO_SOURCE) {
        play(handle);
    }
}

} // namespace audio
} // namespace jupiter
```

**File:** `audio/src/audio.cpp` (REWRITE - Basic stub for now)
```cpp
#include "audio/audio.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>

namespace jupiter {
namespace audio {

namespace {
    bool initialized = false;
    AudioConfig config;
}

bool initialize(const AudioConfig& cfg) {
    if (initialized) {
        return true;
    }

    config = cfg;

    // SDL audio initialization will go here
    LOG_INFO("Audio", "Audio system initialized (stub)");
    LOG_INFO("Audio", "Sample rate: %u Hz, Channels: %u, Buffer: %u samples",
             config.sampleRate, config.channels, config.bufferSize);

    initialized = true;
    return true;
}

void shutdown() {
    if (!initialized) {
        return;
    }

    // SDL audio cleanup will go here
    LOG_INFO("Audio", "Audio system shut down");
    initialized = false;
}

AudioSourceHandle loadSound(const char* path) {
    if (!initialized) {
        LOG_ERROR("Audio", "Audio system not initialized");
        return INVALID_AUDIO_SOURCE;
    }

    LOG_INFO("Audio", "Loading sound: %s (stub)", path);
    // TODO: Actual loading implementation
    return 0;  // Return first slot for now
}

void unloadSound(AudioSourceHandle handle) {
    LOG_INFO("Audio", "Unloading sound handle %u (stub)", handle);
}

void play(AudioSourceHandle handle, bool loop) {
    LOG_INFO("Audio", "Playing sound handle %u, loop=%d (stub)", handle, loop);
}

void stop(AudioSourceHandle handle) {
    LOG_INFO("Audio", "Stopping sound handle %u (stub)", handle);
}

void setVolume(AudioSourceHandle handle, float volume) {
    LOG_INFO("Audio", "Setting volume for handle %u to %.2f (stub)", handle, volume);
}

float getPosition(AudioSourceHandle handle) {
    return 0.0f;
}

bool isPlaying(AudioSourceHandle handle) {
    return false;
}

void update() {
    // Process audio commands from lock-free queue
    // TODO: Implement
}

} // namespace audio
} // namespace jupiter
```

---

## Phase 5: Implement Input System with SDL

### Step 5.1: Design Event-Sourced Input Architecture

**File:** `input/include/input/input.h` (REWRITE)
```cpp
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

#include <SDL3/SDL.h>
#include <cstdint>
#include <array>
#include <atomic>

namespace jupiter {
namespace input {

/**
 * @brief Keyboard key codes (maps to SDL scancodes)
 */
enum class Key : uint16_t {
    A = SDL_SCANCODE_A,
    B = SDL_SCANCODE_B,
    C = SDL_SCANCODE_C,
    D = SDL_SCANCODE_D,
    E = SDL_SCANCODE_E,
    F = SDL_SCANCODE_F,
    G = SDL_SCANCODE_G,
    H = SDL_SCANCODE_H,
    I = SDL_SCANCODE_I,
    J = SDL_SCANCODE_J,
    K = SDL_SCANCODE_K,
    L = SDL_SCANCODE_L,
    M = SDL_SCANCODE_M,
    N = SDL_SCANCODE_N,
    O = SDL_SCANCODE_O,
    P = SDL_SCANCODE_P,
    Q = SDL_SCANCODE_Q,
    R = SDL_SCANCODE_R,
    S = SDL_SCANCODE_S,
    T = SDL_SCANCODE_T,
    U = SDL_SCANCODE_U,
    V = SDL_SCANCODE_V,
    W = SDL_SCANCODE_W,
    X = SDL_SCANCODE_X,
    Y = SDL_SCANCODE_Y,
    Z = SDL_SCANCODE_Z,

    SPACE = SDL_SCANCODE_SPACE,
    ESCAPE = SDL_SCANCODE_ESCAPE,
    RETURN = SDL_SCANCODE_RETURN,
    TAB = SDL_SCANCODE_TAB,
    BACKSPACE = SDL_SCANCODE_BACKSPACE,

    UP = SDL_SCANCODE_UP,
    DOWN = SDL_SCANCODE_DOWN,
    LEFT = SDL_SCANCODE_LEFT,
    RIGHT = SDL_SCANCODE_RIGHT,

    LSHIFT = SDL_SCANCODE_LSHIFT,
    RSHIFT = SDL_SCANCODE_RSHIFT,
    LCTRL = SDL_SCANCODE_LCTRL,
    RCTRL = SDL_SCANCODE_RCTRL,
    LALT = SDL_SCANCODE_LALT,
    RALT = SDL_SCANCODE_RALT
};

/**
 * @brief Mouse button codes
 */
enum class MouseButton : uint8_t {
    LEFT = SDL_BUTTON_LEFT,
    MIDDLE = SDL_BUTTON_MIDDLE,
    RIGHT = SDL_BUTTON_RIGHT,
    X1 = SDL_BUTTON_X1,
    X2 = SDL_BUTTON_X2
};

/**
 * @brief Initialize input system
 */
INPUT_API bool initialize();

/**
 * @brief Shutdown input system
 */
INPUT_API void shutdown();

/**
 * @brief Update input state (call once per frame before game logic)
 *
 * Processes SDL input events and updates state arrays.
 * Lock-free, read SDL event queue and write to our state.
 */
INPUT_API void update();

/**
 * @brief Check if key is currently pressed
 */
INPUT_API bool isKeyDown(Key key);

/**
 * @brief Check if key was just pressed this frame
 */
INPUT_API bool isKeyPressed(Key key);

/**
 * @brief Check if key was just released this frame
 */
INPUT_API bool isKeyReleased(Key key);

/**
 * @brief Get mouse position
 */
INPUT_API void getMousePosition(int32_t* x, int32_t* y);

/**
 * @brief Get mouse delta since last frame
 */
INPUT_API void getMouseDelta(int32_t* dx, int32_t* dy);

/**
 * @brief Check if mouse button is pressed
 */
INPUT_API bool isMouseButtonDown(MouseButton button);

} // namespace input
} // namespace jupiter
```

**File:** `input/src/input.cpp` (REWRITE - Basic stub for now)
```cpp
#include "input/input.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>

namespace jupiter {
namespace input {

namespace {
    bool initialized = false;
}

bool initialize() {
    if (initialized) {
        return true;
    }

    LOG_INFO("Input", "Input system initialized (stub)");
    initialized = true;
    return true;
}

void shutdown() {
    if (!initialized) {
        return;
    }

    LOG_INFO("Input", "Input system shut down");
    initialized = false;
}

void update() {
    // Process SDL input events
    // TODO: Implement
}

bool isKeyDown(Key key) {
    // TODO: Implement
    return false;
}

bool isKeyPressed(Key key) {
    // TODO: Implement
    return false;
}

bool isKeyReleased(Key key) {
    // TODO: Implement
    return false;
}

void getMousePosition(int32_t* x, int32_t* y) {
    if (x) *x = 0;
    if (y) *y = 0;
    // TODO: Implement
}

void getMouseDelta(int32_t* dx, int32_t* dy) {
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    // TODO: Implement
}

bool isMouseButtonDown(MouseButton button) {
    // TODO: Implement
    return false;
}

} // namespace input
} // namespace jupiter
```

---

## Phase 6: Update CMake Build System

### Step 6.1: Update Platform Module CMakeLists.txt

**File:** `platform/CMakeLists.txt` (MODIFY)
```cmake
add_library(platform
    src/platform.cpp
    src/sdl_wrapper.cpp       # NEW
    src/window_sdl.cpp        # NEW
)

target_include_directories(platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(platform
    PUBLIC
        SDL3::SDL3-static
        logging
)

# Find Vulkan for surface creation
find_package(Vulkan REQUIRED)
target_link_libraries(platform PUBLIC ${Vulkan_LIBRARIES})
target_include_directories(platform PUBLIC ${Vulkan_INCLUDE_DIRS})

# Platform-specific settings
if(APPLE)
    target_compile_definitions(platform PRIVATE VK_USE_PLATFORM_METAL_EXT)
elseif(WIN32)
    target_compile_definitions(platform PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    target_compile_definitions(platform PRIVATE VK_USE_PLATFORM_XCB_KHR)
endif()

# Set library properties
set_target_properties(platform PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/platform/platform.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(platform
        PRIVATE PLATFORM_EXPORTS
        INTERFACE PLATFORM_IMPORTS
    )
endif()
```

### Step 6.2: Update Audio/Input CMakeLists.txt

**File:** `audio/CMakeLists.txt` (MODIFY)
```cmake
add_library(audio
    src/audio.cpp
)

target_include_directories(audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(audio
    PUBLIC
        platform  # Platform now provides SDL
        logging
)

# Set library properties
set_target_properties(audio PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/audio/audio.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(audio
        PRIVATE AUDIO_EXPORTS
        INTERFACE AUDIO_IMPORTS
    )
endif()
```

**File:** `input/CMakeLists.txt` (MODIFY)
```cmake
add_library(input
    src/input.cpp
)

target_include_directories(input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(input
    PUBLIC
        platform  # Platform now provides SDL
        logging
)

# Set library properties
set_target_properties(input PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/input/input.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(input
        PRIVATE INPUT_EXPORTS
        INTERFACE INPUT_IMPORTS
    )
endif()
```

---

## Phase 7: Testing and Validation

### Step 7.1: Verify Vulkan Triangle Still Works

The existing `projects/vulkan_triangle/` should work unchanged because the rendering API abstraction hides SDL.

**Test steps:**
```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target vulkan_triangle
./bin/vulkan_triangle
```

**Expected result:** Triangle renders correctly with no regressions.

---

## Phase 8: Migration Checklist

### Files to CREATE:

#### Phase 1:
- [ ] `vendored/README.md` - Document SDL3 and deprecate GLFW
- [ ] `vendored/CMakeLists.txt` - SDL3 build config

#### Phase 2:
- [ ] `platform/include/platform/sdl_wrapper.h`
- [ ] `platform/src/sdl_wrapper.cpp`
- [ ] `platform/include/platform/window.h`
- [ ] `platform/src/window_sdl.cpp`

#### Phase 4:
- [ ] `audio/include/audio/audio.h` (rewrite)
- [ ] `audio/src/audio.cpp` (rewrite with SDL_Audio stubs)

#### Phase 5:
- [ ] `input/include/input/input.h` (rewrite)
- [ ] `input/src/input.cpp` (rewrite with SDL_Event stubs)

### Files to MODIFY:

#### Phase 1:
- [ ] `CMakeLists.txt` (root) - Add vendored/ subdirectory

#### Phase 2:
- [ ] `platform/CMakeLists.txt` - Add SDL sources, link SDL3

#### Phase 3:
- [ ] `rendering/CMakeLists.txt` - Remove GLFW, update includes
- [ ] `rendering/include/rendering/rendering.h` - Remove GLFW include
- [ ] `rendering/src/rendering.cpp` - Use platform::SDLWindow
- [ ] `rendering/src/application.cpp` - Use SDL timing

#### Phase 4:
- [ ] `audio/CMakeLists.txt` - Link platform (provides SDL)

#### Phase 5:
- [ ] `input/CMakeLists.txt` - Link platform (provides SDL)

#### Phase 6:
- [ ] `CLAUDE.md` - Update tech stack to list SDL3

### Files to DELETE (after migration complete):
- [ ] `vendored/glfw/` - Remove entire GLFW directory

---

## Phase 9: Build and Test Steps

```bash
# 1. Clone SDL3
cd /Users/bengamble/project-jupiter/vendored
git clone https://github.com/libsdl-org/SDL.git sdl3
cd sdl3
git checkout main

# 2. Clean build directory
cd /Users/bengamble/project-jupiter
rm -rf build && mkdir build && cd build

# 3. Configure with SDL3
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 4. Build platform module (should work with SDL)
cmake --build . --target platform

# 5. Build rendering module (should work with SDL via platform)
cmake --build . --target rendering

# 6. Build vulkan_triangle (should work unchanged)
cmake --build . --target vulkan_triangle

# 7. Run triangle test
./bin/vulkan_triangle
# Expected: Triangle renders correctly

# 8. Check SDL version in logs
# Expected: "SDL initialized successfully (version 3.x.x)"
```

---

## Phase 10: Success Criteria

✅ **Build System:**
- [ ] CMake configures without errors
- [ ] All modules build successfully
- [ ] SDL3 links properly
- [ ] No GLFW references remain (after cleanup)

✅ **Functionality:**
- [ ] Vulkan triangle renders correctly
- [ ] Window creation/destruction works
- [ ] Events handled properly (close window)
- [ ] No regressions in existing features

✅ **Performance:**
- [ ] No additional allocations in hot paths
- [ ] Frame time unchanged from GLFW version
- [ ] Memory usage similar or better

✅ **Architecture:**
- [ ] SDL only in platform/audio/input modules
- [ ] Clean dependency graph
- [ ] Follows CLAUDE.md principles

---

## Phase 11: Future Work (Full Audio/Input Implementation)

After basic migration is complete, implement full audio and input:

### Audio (Phase 11a):
1. SDL_AudioStream for playback
2. Lock-free command queue (ring buffer)
3. Pre-allocated audio buffers
4. OGG/MP3 loading via SDL_LoadWAV
5. Spatial audio (3D positioning)

### Input (Phase 11b):
1. Full keyboard state tracking
2. Mouse button/motion tracking
3. Gamepad support via SDL_Gamepad
4. Event sourcing (ring buffer of InputEvent)
5. Input replay system

---

## Troubleshooting

### Common Issues:

**1. SDL3 not found:**
```bash
# Ensure SDL3 is cloned correctly
ls vendored/sdl3/include/SDL3/SDL.h
# Should exist
```

**2. Vulkan surface creation fails:**
```bash
# Check SDL window created with Vulkan flag
# In window_sdl.cpp: SDL_WINDOW_VULKAN flag must be set
```

**3. Linker errors:**
```bash
# Ensure platform links SDL3::SDL3-static
# Check platform/CMakeLists.txt
```

**4. SDL_GetError() shows "Video subsystem not initialized":**
```bash
# Ensure platform::sdl::initialize() called before window creation
```

---

## References

- **SDL3 API Docs**: https://wiki.libsdl.org/SDL3
- **SDL Vulkan Guide**: https://wiki.libsdl.org/SDL3/CategoryVulkan
- **SDL Audio Guide**: https://wiki.libsdl.org/SDL3/CategoryAudio
- **Lock-Free Programming**: https://preshing.com/20120612/an-introduction-to-lock-free-programming/

---

## Summary

This migration guide provides a complete, step-by-step plan to migrate Project Jupiter from GLFW to SDL3. The migration:

1. **Maintains architectural principles** - Lock-free, no runtime allocation, event sourcing
2. **Improves platform support** - Single library for windowing, audio, input, threading
3. **Enables future features** - Gamepad support, spatial audio, mobile platforms
4. **No breaking changes** - Existing code continues to work via abstraction layers
5. **Battle-tested** - SDL powers thousands of commercial games

Follow each phase in order, testing at each step. The vulkan_triangle demo provides continuous validation that rendering still works correctly.
