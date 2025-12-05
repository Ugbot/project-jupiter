#pragma once

/**
 * @file pipeline_debug.h
 * @brief Debug visualization pipeline for Jupiter engine
 *
 * Renders debug primitives:
 * - AABBs (bounding boxes)
 * - Frustums (camera/light frustums)
 * - Light volumes (point/spot lights)
 * - Infinite grid
 * - Lines and wireframes
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <vector>
#include <memory>

namespace jupiter::rendering {

namespace vulkan {
    class VulkanBuffer;
}

/**
 * @brief Debug line vertex
 */
struct DebugVertex {
    glm::vec3 position;
    glm::vec4 color;
};

/**
 * @brief AABB to render
 */
struct DebugAABB {
    glm::vec3 min;
    glm::vec3 max;
    glm::vec4 color;
};

/**
 * @brief Frustum to render
 */
struct DebugFrustum {
    glm::vec3 corners[8];  // Near: BL, BR, TR, TL; Far: BL, BR, TR, TL
    glm::vec4 color;
};

/**
 * @brief Debug visualization configuration
 */
struct DebugConfig {
    bool showAABBs = false;
    bool showFrustums = false;
    bool showLightVolumes = false;
    bool showGrid = true;
    bool showWireframe = false;

    float gridSize = 100.0f;
    float gridSpacing = 1.0f;
    glm::vec4 gridColor = glm::vec4(0.3f, 0.3f, 0.3f, 0.5f);
    glm::vec4 gridAxisColorX = glm::vec4(1.0f, 0.2f, 0.2f, 0.8f);
    glm::vec4 gridAxisColorZ = glm::vec4(0.2f, 0.2f, 1.0f, 0.8f);

    float lineWidth = 1.0f;
};

/**
 * @brief Debug visualization pipeline
 *
 * Renders debug geometry as lines with depth testing.
 * Uses a simple vertex shader and solid color fragment shader.
 */
class PipelineDebug {
public:
    static constexpr uint32_t MAX_DEBUG_VERTICES = 100000;
    static constexpr uint32_t MAX_LINES = MAX_DEBUG_VERTICES / 2;

    PipelineDebug() = default;
    ~PipelineDebug();

    // Non-copyable
    PipelineDebug(const PipelineDebug&) = delete;
    PipelineDebug& operator=(const PipelineDebug&) = delete;

    /**
     * @brief Initialize debug pipeline
     */
    bool initialize(VkDevice device, VmaAllocator allocator,
                   VkRenderPass renderPass, VkFormat depthFormat,
                   uint32_t width, uint32_t height);

    void destroy();

    /**
     * @brief Set view-projection matrix for rendering
     */
    void setViewProjection(const glm::mat4& viewProj);

    /**
     * @brief Begin a new frame of debug drawing
     * Clears all queued debug primitives
     */
    void beginFrame();

    /**
     * @brief Queue an AABB for rendering
     */
    void drawAABB(const glm::vec3& min, const glm::vec3& max,
                  const glm::vec4& color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

    /**
     * @brief Queue a frustum for rendering
     */
    void drawFrustum(const glm::vec3 corners[8],
                     const glm::vec4& color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));

    /**
     * @brief Queue a frustum from view-projection matrix
     */
    void drawFrustumFromMatrix(const glm::mat4& invViewProj,
                               const glm::vec4& color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));

    /**
     * @brief Queue a sphere for rendering (as wireframe)
     */
    void drawSphere(const glm::vec3& center, float radius,
                    const glm::vec4& color = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),
                    int segments = 16);

    /**
     * @brief Queue a line for rendering
     */
    void drawLine(const glm::vec3& start, const glm::vec3& end,
                  const glm::vec4& color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    /**
     * @brief Draw an infinite grid at Y=0
     */
    void drawGrid(float size, float spacing,
                  const glm::vec4& color = glm::vec4(0.3f, 0.3f, 0.3f, 0.5f));

    /**
     * @brief End frame and upload vertex data
     */
    void endFrame(VkCommandBuffer cmd);

    /**
     * @brief Record draw commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd);

    [[nodiscard]] bool isInitialized() const { return pipeline_ != VK_NULL_HANDLE; }
    [[nodiscard]] uint32_t getVertexCount() const { return static_cast<uint32_t>(vertices_.size()); }

    DebugConfig& config() { return config_; }
    const DebugConfig& config() const { return config_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkShaderModule vertShader_ = VK_NULL_HANDLE;
    VkShaderModule fragShader_ = VK_NULL_HANDLE;

    std::unique_ptr<vulkan::VulkanBuffer> vertexBuffer_;
    std::vector<DebugVertex> vertices_;

    glm::mat4 viewProj_ = glm::mat4(1.0f);
    DebugConfig config_;

    bool createShaders();
    bool createPipeline(VkRenderPass renderPass, VkFormat depthFormat);

    void addVertex(const glm::vec3& pos, const glm::vec4& color);
};

} // namespace jupiter::rendering
