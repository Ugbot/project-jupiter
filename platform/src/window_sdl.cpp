#include "platform/platform.h"
#include "platform/sdl_wrapper.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <memory>

namespace jupiter {
namespace platform {

/**
 * @brief SDL-based window implementation
 *
 * This class implements the Window interface using SDL3. It provides
 * platform-agnostic windowing that works on Windows, macOS, and Linux.
 */
class WindowSDL : public Window {
public:
    WindowSDL(SDL_Window* window, const CreateParams& params)
        : window_(window)
        , params_(params)
        , should_close_(false)
    {
    }

    ~WindowSDL() override {
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    // Non-copyable, non-movable
    WindowSDL(const WindowSDL&) = delete;
    WindowSDL& operator=(const WindowSDL&) = delete;
    WindowSDL(WindowSDL&&) = delete;
    WindowSDL& operator=(WindowSDL&&) = delete;

    bool processMessages() override {
        // Only pump events to update event state; don't consume them here.
        // The application's onInput() will poll and consume events directly.
        // This allows window to check for quit/resize in the event loop while
        // leaving input events for the application to handle.
        SDL_PumpEvents();
        
        // Check for quit event without consuming it
        SDL_Event event;
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0) {
            should_close_ = true;
            return false;
        }
        
        // Check for window close without consuming other events
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 
                              SDL_EVENT_WINDOW_CLOSE_REQUESTED) > 0) {
            if (event.window.windowID == SDL_GetWindowID(window_)) {
                should_close_ = true;
                return false;
            }
        }
        
        // Check for window resize without consuming other events
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_RESIZED,
                              SDL_EVENT_WINDOW_RESIZED) > 0) {
            if (event.window.windowID == SDL_GetWindowID(window_)) {
                params_.width = event.window.data1;
                params_.height = event.window.data2;
            }
        }

        return !should_close_;
    }
    
    void requestClose() override {
        should_close_ = true;
    }

    int getWidth() const override {
        int width = 0;
        SDL_GetWindowSize(window_, &width, nullptr);
        return width;
    }

    int getHeight() const override {
        int height = 0;
        SDL_GetWindowSize(window_, nullptr, &height);
        return height;
    }

    void setTitle(const std::string& title) override {
        params_.title = title;
        SDL_SetWindowTitle(window_, title.c_str());
    }

    bool shouldClose() const override {
        return should_close_;
    }

    void show() override {
        SDL_ShowWindow(window_);
    }

    void hide() override {
        SDL_HideWindow(window_);
    }

    int createVulkanSurface(void* instance, void** surface) override {
        if (!window_ || !instance || !surface) {
            return -1;
        }

        // Cast void* to proper Vulkan types
        VkInstance vkInstance = static_cast<VkInstance>(instance);
        VkSurfaceKHR vkSurface = VK_NULL_HANDLE;

        // Use SDL's Vulkan surface creation
        if (!SDL_Vulkan_CreateSurface(window_, vkInstance, nullptr, &vkSurface)) {
            std::cerr << "[WindowSDL] Failed to create Vulkan surface: " << SDL_GetError() << std::endl;
            return -1;
        }

        // Store the surface handle in the output parameter
        *surface = static_cast<void*>(vkSurface);
        return 0;
    }

    /**
     * @brief Get the native SDL window handle
     * @return SDL_Window pointer
     */
    SDL_Window* getNativeWindow() const {
        return window_;
    }

private:
    SDL_Window* window_;
    CreateParams params_;
    bool should_close_;
};

// ============================================================================
// Window Factory Implementation
// ============================================================================

std::unique_ptr<Window> Window::create(const CreateParams& params) {
    // Ensure SDL is initialized
    if (!sdl::isSDLInitialized()) {
        std::cerr << "[Window] Cannot create window: SDL not initialized" << std::endl;
        return nullptr;
    }

    // Build SDL window flags
    Uint32 flags = SDL_WINDOW_VULKAN; // Enable Vulkan support by default

    if (params.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    if (params.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Create SDL window
    SDL_Window* sdl_window = SDL_CreateWindow(
        params.title.c_str(),
        params.width,
        params.height,
        flags
    );

    if (!sdl_window) {
        std::cerr << "[Window] Failed to create window: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    // Set window position if specified (SDL3 handles this differently)
    if (params.x >= 0 && params.y >= 0) {
        SDL_SetWindowPosition(sdl_window, params.x, params.y);
    }

    std::cout << "[Window] Created: \"" << params.title << "\" "
              << "(" << params.width << "x" << params.height << ")" << std::endl;

    // Wrap in our WindowSDL implementation
    return std::unique_ptr<Window>(new WindowSDL(sdl_window, params));
}

} // namespace platform
} // namespace jupiter
