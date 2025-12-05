#include <iostream>
#include "rendering/rendering.h"
#include "logging/logging.h"

using namespace jupiter::rendering;

/**
 * @brief Simple Vulkan triangle demo using the Application framework
 *
 * This demonstrates the ease of use of the Jupiter engine's rendering API.
 * Users simply:
 * 1. Extend Application
 * 2. Override onInit() to setup resources
 * 3. Override onRender() to draw
 * 4. Call run()
 *
 * All Vulkan complexity is hidden behind a simple, clean API.
 */
class TriangleApplication : public Application {
public:
    TriangleApplication()
        : Application("Vulkan Triangle Demo", 800, 600, false)  // false = disable validation for now
        , vertexBuffer_(nullptr)
        , indexBuffer_(nullptr)
        , indexCount_(0) {
    }

    ~TriangleApplication() override = default;

protected:
    void onInit() override {
        LOG_INFO("TriangleApp", "Initializing triangle demo");

        // Define triangle vertices (user-facing, explicit)
        // Each vertex has: position (x, y) and color (r, g, b)
        Vertex vertices[] = {
            {{  0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f }},  // Bottom - Red
            {{  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f }},  // Top Right - Green
            {{ -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }}   // Top Left - Blue
        };

        // Define triangle indices (clockwise winding)
        uint16_t indices[] = { 0, 1, 2 };
        indexCount_ = 3;

        // Create vertex buffer and upload data
        vertexBuffer_ = createVertexBuffer(vertices, 3);
        if (!vertexBuffer_) {
            LOG_ERROR("TriangleApp", "Failed to create vertex buffer");
            return;
        }

        // Create index buffer and upload data
        indexBuffer_ = createIndexBuffer(indices, indexCount_);
        if (!indexBuffer_) {
            LOG_ERROR("TriangleApp", "Failed to create index buffer");
            return;
        }

        // Load shaders from shaders/ directory
        if (!loadShaders("shaders/triangle.vert.spv", "shaders/triangle.frag.spv")) {
            LOG_ERROR("TriangleApp", "Failed to load shaders");
            return;
        }

        LOG_INFO("TriangleApp", "Triangle demo initialized successfully");
        LOG_INFO("TriangleApp", "Close the window or press Ctrl+C to exit");
    }

    void onUpdate(float deltaTime) override {
        // Could add animation, input handling, etc. here
        // For this simple demo, we don't need any updates
        (void)deltaTime;  // Unused
    }

    void onRender() override {
        // Simple rendering: bind pipeline and draw indexed triangle
        bindPipeline();
        drawIndexed(vertexBuffer_, indexBuffer_, indexCount_);
    }

    void onShutdown() override {
        LOG_INFO("TriangleApp", "Shutting down triangle demo");
        // Buffers are automatically cleaned up by Application base class
    }

private:
    // Resources (managed by Application base class)
    vulkan::VulkanBuffer* vertexBuffer_;
    vulkan::VulkanBuffer* indexBuffer_;
    uint32_t indexCount_;
};

int main(int argc, char* argv[]) {
    // Initialize logging
    jupiter::logging::initialize("vulkan_triangle.log");

    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "  Vulkan Triangle Demo");
    LOG_INFO("Main", "  Project Jupiter Engine");
    LOG_INFO("Main", "========================================");

    // Create and run application
    TriangleApplication app;
    int result = app.run();

    // Shutdown logging
    jupiter::logging::shutdown();

    return result;
}
