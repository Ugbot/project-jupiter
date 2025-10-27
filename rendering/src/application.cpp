#include "rendering/application.h"
#include "vulkan_backend.h"
#include "logging/logging.h"
#include "platform/platform.h"

#include <GLFW/glfw3.h>

namespace jupiter {
namespace rendering {

Application::Application(const std::string& title, uint32_t width, uint32_t height,
                         bool enableValidation)
    : title_(title)
    , width_(width)
    , height_(height)
    , enableValidation_(enableValidation)
    , lastFrameTime_(0.0) {
}

Application::~Application() {
    shutdown();
}

int Application::run() {
    LOG_INFO("Application", "Starting application: %s", title_.c_str());

    if (!initialize()) {
        LOG_ERROR("Application", "Failed to initialize application");
        return EXIT_FAILURE;
    }

    // Call user initialization
    LOG_INFO("Application", "Calling onInit()");
    onInit();

    // Run main loop
    LOG_INFO("Application", "Entering main loop");
    mainLoop();

    // Call user shutdown
    LOG_INFO("Application", "Calling onShutdown()");
    onShutdown();

    // Cleanup
    shutdown();

    LOG_INFO("Application", "Application completed successfully");
    return EXIT_SUCCESS;
}

bool Application::initialize() {
    // Initialize logging if not already done
    if (!enableValidation_) {
        jupiter::logging::initialize();
    }

    // Create window
    LOG_INFO("Application", "Creating window (%dx%d)", width_, height_);
    window_ = std::make_unique<Window>();

    WindowConfig windowConfig;
    windowConfig.title = title_;
    windowConfig.width = width_;
    windowConfig.height = height_;
    windowConfig.resizable = false;
    windowConfig.visible = true;

    if (!window_->initialize(windowConfig)) {
        LOG_ERROR("Application", "Failed to create window");
        return false;
    }

    // Initialize Vulkan renderer
    LOG_INFO("Application", "Initializing Vulkan renderer");
    renderer_ = std::make_unique<vulkan::VulkanRenderer>();

    if (!renderer_->initialize(*window_, enableValidation_)) {
        LOG_ERROR("Application", "Failed to initialize Vulkan renderer");
        return false;
    }

    // Initialize timing
    lastFrameTime_ = getCurrentTime();

    LOG_INFO("Application", "Application initialized successfully");
    return true;
}

void Application::shutdown() {
    if (renderer_) {
        renderer_->waitIdle();
    }

    // Clear all user-created buffers
    buffers_.clear();

    // Destroy renderer and window
    renderer_.reset();
    window_.reset();

    LOG_INFO("Application", "Application shut down");
}

void Application::mainLoop() {
    while (!shouldClose()) {
        // Poll window events
        window_->pollEvents();

        // Calculate delta time
        double currentTime = getCurrentTime();
        float deltaTime = static_cast<float>(currentTime - lastFrameTime_);
        lastFrameTime_ = currentTime;

        // Call user update
        onUpdate(deltaTime);

        // Begin frame
        uint32_t imageIndex;
        if (!renderer_->beginFrame(imageIndex)) {
            LOG_WARN("Application", "Failed to begin frame, skipping");
            continue;
        }

        // Begin render pass
        renderer_->beginRenderPass(imageIndex);

        // Call user render
        onRender();

        // End render pass
        renderer_->endRenderPass();

        // End frame
        renderer_->endFrame(imageIndex);
    }

    // Wait for device to finish before returning
    renderer_->waitIdle();
}

double Application::getCurrentTime() const {
    return glfwGetTime();
}

bool Application::shouldClose() const {
    return window_ && window_->shouldClose();
}

// ============================================================================
// Helper Methods
// ============================================================================

vulkan::VulkanBuffer* Application::createVertexBuffer(const Vertex* vertices,
                                                      uint32_t vertexCount) {
    auto buffer = std::make_unique<vulkan::VulkanBuffer>();

    VkDeviceSize bufferSize = sizeof(Vertex) * vertexCount;
    if (!renderer_->createVertexBuffer(vertices, bufferSize, *buffer)) {
        LOG_ERROR("Application", "Failed to create vertex buffer");
        return nullptr;
    }

    auto* ptr = buffer.get();
    buffers_.push_back(std::move(buffer));

    LOG_INFO("Application", "Created vertex buffer (%d vertices)", vertexCount);
    return ptr;
}

vulkan::VulkanBuffer* Application::createIndexBuffer(const uint16_t* indices,
                                                     uint32_t indexCount) {
    auto buffer = std::make_unique<vulkan::VulkanBuffer>();

    VkDeviceSize bufferSize = sizeof(uint16_t) * indexCount;
    if (!renderer_->createIndexBuffer(indices, bufferSize, *buffer)) {
        LOG_ERROR("Application", "Failed to create index buffer");
        return nullptr;
    }

    auto* ptr = buffer.get();
    buffers_.push_back(std::move(buffer));

    LOG_INFO("Application", "Created index buffer (%d indices)", indexCount);
    return ptr;
}

bool Application::loadShaders(const std::string& vertShaderFile,
                              const std::string& fragShaderFile) {
    LOG_INFO("Application", "Loading shaders: %s, %s",
             vertShaderFile.c_str(), fragShaderFile.c_str());

    if (!renderer_->createPipeline(vertShaderFile, fragShaderFile)) {
        LOG_ERROR("Application", "Failed to create pipeline");
        return false;
    }

    LOG_INFO("Application", "Shaders loaded successfully");
    return true;
}

void Application::bindPipeline() {
    renderer_->bindPipeline();
}

void Application::drawIndexed(vulkan::VulkanBuffer* vertexBuffer,
                              vulkan::VulkanBuffer* indexBuffer,
                              uint32_t indexCount) {
    if (!vertexBuffer || !indexBuffer) {
        LOG_ERROR("Application", "Invalid buffer passed to drawIndexed");
        return;
    }

    renderer_->drawIndexed(*vertexBuffer, *indexBuffer, indexCount);
}

} // namespace rendering
} // namespace jupiter
