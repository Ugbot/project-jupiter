#include "rendering/rendering.h"
#include "../../platform/include/platform/platform.h"
#include "../../math/include/math/math.h"

// Vulkan includes
#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <array>
#include <set>
#include <chrono>

// Logging for Vulkan implementation
#include "../../logging/include/logging/logging.h"

namespace jupiter {
namespace rendering {

// ============================================================================
// Window Implementation
// ============================================================================

Window::Window() : m_window(nullptr) {}

Window::~Window() {
    shutdown();
}

bool Window::initialize(const WindowConfig& config) {
    m_config = config;

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Tell GLFW not to create an OpenGL context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    if (!config.visible) {
        glfwHideWindow(m_window);
    }

    return true;
}

void Window::shutdown() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents() {
    glfwPollEvents();
}

uint32_t Window::getWidth() const {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    return static_cast<uint32_t>(width);
}

uint32_t Window::getHeight() const {
    int width, height;
    glfwGetWindowSize(m_window, &width, &height);
    return static_cast<uint32_t>(height);
}

VkResult Window::createVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    return glfwCreateWindowSurface(instance, m_window, nullptr, surface);
}

std::vector<const char*> Window::getRequiredExtensions() const {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
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
