#pragma once

#include "rendering.h"
#include <string>
#include <memory>

namespace jupiter {
namespace rendering {

// Forward declarations
namespace vulkan {
    class VulkanRenderer;
    class VulkanBuffer;
}

/**
 * @brief Base application class for easy Vulkan application development
 *
 * Users extend this class and override lifecycle methods:
 * - onInit(): Setup resources (create meshes, load shaders, etc.)
 * - onUpdate(deltaTime): Update game logic
 * - onRender(): Issue rendering commands
 * - onShutdown(): Cleanup resources
 *
 * The Application class manages the window, Vulkan initialization, and main loop.
 *
 * Example usage:
 * @code
 * class MyApp : public Application {
 * public:
 *     MyApp() : Application("My App", 800, 600) {}
 *
 *     void onInit() override {
 *         // Create mesh, load shaders
 *     }
 *
 *     void onRender() override {
 *         // Issue draw commands
 *     }
 * };
 *
 * int main() {
 *     MyApp app;
 *     return app.run();
 * }
 * @endcode
 */
class RENDERING_API Application {
public:
    /**
     * @brief Construct an application
     * @param title Window title
     * @param width Window width
     * @param height Window height
     * @param enableValidation Enable Vulkan validation layers (useful for debugging)
     */
    Application(const std::string& title, uint32_t width, uint32_t height,
                bool enableValidation = false);

    virtual ~Application();

    // No copy, allow move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = default;
    Application& operator=(Application&&) = default;

    /**
     * @brief Run the application
     *
     * This method:
     * 1. Initializes the window and Vulkan
     * 2. Calls onInit()
     * 3. Runs the main loop (onUpdate + onRender)
     * 4. Calls onShutdown()
     * 5. Cleans up resources
     *
     * @return Exit code (0 for success, non-zero for failure)
     */
    int run();

protected:
    // ========================================================================
    // Lifecycle Methods - Override these in your application
    // ========================================================================

    /**
     * @brief Called once after initialization, before main loop
     *
     * Override this to create meshes, load shaders, setup scene, etc.
     * Use helper methods like createVertexBuffer, createIndexBuffer, loadShaders.
     */
    virtual void onInit() {}

    /**
     * @brief Called every frame for game logic updates
     * @param deltaTime Time since last frame in seconds
     *
     * Override this for animation, physics, input handling, etc.
     */
    virtual void onUpdate(float deltaTime) {}

    /**
     * @brief Called every frame for rendering
     *
     * Override this to issue draw commands using helper methods like:
     * - bindPipeline()
     * - drawIndexed()
     */
    virtual void onRender() = 0;  // Pure virtual - must implement

    /**
     * @brief Called once before shutdown
     *
     * Override this to cleanup custom resources.
     * Window and Vulkan resources are cleaned up automatically.
     */
    virtual void onShutdown() {}

    // ========================================================================
    // Helper Methods - Use these in your onInit() and onRender()
    // ========================================================================

    /**
     * @brief Vertex structure for simple colored geometry
     */
    struct Vertex {
        float pos[2];    // Position (x, y)
        float color[3];  // Color (r, g, b)
    };

    /**
     * @brief Create a vertex buffer from vertex data
     * @param vertices Pointer to vertex data
     * @param vertexCount Number of vertices
     * @return Pointer to created buffer (managed internally, don't delete)
     */
    vulkan::VulkanBuffer* createVertexBuffer(const Vertex* vertices, uint32_t vertexCount);

    /**
     * @brief Create an index buffer from index data
     * @param indices Pointer to index data (uint16_t)
     * @param indexCount Number of indices
     * @return Pointer to created buffer (managed internally, don't delete)
     */
    vulkan::VulkanBuffer* createIndexBuffer(const uint16_t* indices, uint32_t indexCount);

    /**
     * @brief Load and create a graphics pipeline from shader files
     * @param vertShaderFile Vertex shader filename (e.g., "triangle.vert.spv")
     * @param fragShaderFile Fragment shader filename (e.g., "triangle.frag.spv")
     * @return true if successful
     *
     * Shaders are searched in: ./shaders/, ../shaders/, shaders/
     */
    bool loadShaders(const std::string& vertShaderFile, const std::string& fragShaderFile);

    /**
     * @brief Bind the current pipeline for rendering
     *
     * Call this in onRender() before draw commands.
     */
    void bindPipeline();

    /**
     * @brief Draw indexed geometry
     * @param vertexBuffer Vertex buffer created with createVertexBuffer()
     * @param indexBuffer Index buffer created with createIndexBuffer()
     * @param indexCount Number of indices to draw
     *
     * Call this in onRender() after bindPipeline().
     */
    void drawIndexed(vulkan::VulkanBuffer* vertexBuffer,
                     vulkan::VulkanBuffer* indexBuffer,
                     uint32_t indexCount);

    /**
     * @brief Check if window should close (user clicked X)
     * @return true if window should close
     */
    bool shouldClose() const;

    /**
     * @brief Get window width
     */
    uint32_t getWidth() const { return width_; }

    /**
     * @brief Get window height
     */
    uint32_t getHeight() const { return height_; }

private:
    std::string title_;
    uint32_t width_;
    uint32_t height_;
    bool enableValidation_;

    // Window and renderer (managed internally)
    std::unique_ptr<Window> window_;
    std::unique_ptr<vulkan::VulkanRenderer> renderer_;

    // Buffers created by user (managed internally for cleanup)
    std::vector<std::unique_ptr<vulkan::VulkanBuffer>> buffers_;

    // Timing
    double lastFrameTime_;

    bool initialize();
    void shutdown();
    void mainLoop();
    double getCurrentTime() const;
};

} // namespace rendering
} // namespace jupiter
