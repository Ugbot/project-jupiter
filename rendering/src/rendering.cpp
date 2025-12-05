#include "rendering/rendering.h"
#include "platform/platform.h"
#include "platform/sdl_wrapper.h"
#include "math/math.h"

// Vulkan includes
#include <vulkan/vulkan.h>

// SDL for Vulkan surface creation
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <array>
#include <set>
#include <chrono>

// Logging for Vulkan implementation
#include "logging/logging.h"

namespace jupiter {
namespace rendering {

// ============================================================================
// Window Implementation (SDL-based)
// ============================================================================

Window::Window() : m_window(nullptr) {}

Window::~Window() {
    shutdown();
}

bool Window::initialize(const WindowConfig& config) {
    m_config = config;

    // Ensure SDL is initialized (video subsystem required for windows)
    if (!platform::sdl::isSDLInitialized()) {
        std::cerr << "[Window] SDL not initialized. Please create SDLWrapper first." << std::endl;
        return false;
    }

    // Create platform window using the platform abstraction
    platform::Window::CreateParams params;
    params.title = config.title;
    params.width = config.width;
    params.height = config.height;
    params.x = -1;  // Center window
    params.y = -1;  // Center window
    params.resizable = config.resizable;
    params.fullscreen = false;

    m_window = platform::Window::create(params);
    if (!m_window) {
        std::cerr << "[Window] Failed to create platform window" << std::endl;
        return false;
    }

    // Show or hide window based on config
    if (config.visible) {
        m_window->show();
    } else {
        m_window->hide();
    }

    return true;
}

void Window::shutdown() {
    m_window.reset();
}

bool Window::shouldClose() const {
    return m_window ? m_window->shouldClose() : true;
}

void Window::pollEvents() {
    if (m_window) {
        m_window->processMessages();
    }
}

void Window::requestClose() {
    if (m_window) {
        m_window->requestClose();
    }
}

uint32_t Window::getWidth() const {
    return m_window ? static_cast<uint32_t>(m_window->getWidth()) : 0;
}

uint32_t Window::getHeight() const {
    return m_window ? static_cast<uint32_t>(m_window->getHeight()) : 0;
}

VkResult Window::createVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    if (!m_window || !surface) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Use the platform window's Vulkan surface creation
    void* surfacePtr = nullptr;
    int result = m_window->createVulkanSurface(static_cast<void*>(instance), &surfacePtr);

    if (result != 0) {
        std::cerr << "[Window] Failed to create Vulkan surface via platform window" << std::endl;
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    // Store the surface handle
    *surface = static_cast<VkSurfaceKHR>(surfacePtr);
    return VK_SUCCESS;
}

std::vector<const char*> Window::getRequiredExtensions() const {
    // Use SDL3 to get required Vulkan extensions
    // SDL3 API changed: it now returns a null-terminated array directly
    Uint32 extensionCount = 0;
    const char* const* extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

    if (!extensionNames) {
        std::cerr << "[Window] Failed to get Vulkan extensions: " << SDL_GetError() << std::endl;
        return {};
    }

    // Convert to vector
    std::vector<const char*> extensions;
    extensions.reserve(extensionCount);
    for (Uint32 i = 0; i < extensionCount; ++i) {
        extensions.push_back(extensionNames[i]);
    }

    return extensions;
}

// ============================================================================
// Legacy Renderer Implementation - DEPRECATED
// New code should use the Application class (rendering/application.h)
// which provides a much simpler and cleaner API
// ============================================================================

// Note: The old Renderer singleton is kept for backward compatibility
// but is not fully implemented. See vulkan_backend.cpp and application.cpp
// for the working implementation.

} // namespace rendering
} // namespace jupiter
