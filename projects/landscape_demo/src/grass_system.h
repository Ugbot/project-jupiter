#pragma once

#include "rendering/vulkan_compute_pipeline.h"
#include "rendering/render_globals.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>

// Forward declare VulkanBuffer from internal header
namespace jupiter {
namespace rendering {
namespace vulkan {
    class VulkanBuffer;
}
}
}

namespace landscape {

// Forward declarations
class TrailField;

constexpr uint32_t MAX_GRASS_INSTANCES = 1024 * 1024;  // 1M instances max
constexpr uint32_t BLADE_VERTS = 12;  // 6 segments * 2 verts

/**
 * @brief Grass instance (std430 layout, matches shader)
 */
struct alignas(16) GrassInstance {
    glm::vec4 pos_height;     // xyz = world pos, w = blade height
    glm::vec4 normal_seed;    // xyz = world normal, w = random seed
    glm::vec4 bend_flatten;   // xy = trail bend dir, z = bend amount, w = flatten amount
};

/**
 * @brief Grass rendering parameters
 */
struct GrassParams {
    float grassRadius = 128.0f;      // radius around camera
    float cellSize = 0.5f;           // spacing between candidates
    float minHeight = 0.3f;          // min blade height
    float maxHeight = 1.2f;          // max blade height
    float densityMul = 1.0f;         // density multiplier
    float minNormalY = 0.3f;         // slope cutoff
    float trailDensityKill = 0.7f;   // how much trail kills density
    float minHeightMul = 0.2f;       // trail flattening min
};

/**
 * @brief GPU-generated grass rendering system
 * 
 * Uses compute shader to generate grass instances around camera,
 * with trail-based flattening and bending.
 */
class GrassSystem {
public:
    GrassSystem() = default;
    ~GrassSystem();

    // Non-copyable
    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;

    /**
     * @brief Initialize grass system
     * 
     * @param device Vulkan device
     * @param physicalDevice Physical device
     * @param allocator VMA allocator
     * @param renderPass Main render pass
     * @param renderGlobals Render globals for camera/lights
     * @return true if successful
     */
    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VmaAllocator allocator, VkRenderPass renderPass,
                    jupiter::rendering::RenderGlobals* renderGlobals);

    void destroy();

    /**
     * @brief Bind terrain heightmap for grass generation
     */
    void bindHeightmap(VkImageView heightmapView, VkSampler heightmapSampler,
                       float terrainSize);

    /**
     * @brief Bind trail field for grass generation
     */
    void bindTrailField(TrailField* trailField);

    /**
     * @brief Reset instance counter and indirect command
     */
    void resetCounters(VkCommandBuffer cmd);

    /**
     * @brief Generate grass instances (compute dispatch)
     * 
     * @param cmd Command buffer
     * @param cameraPos Camera world position
     * @param deltaTime Delta time in seconds
     */
    void generateInstances(VkCommandBuffer cmd, const glm::vec3& cameraPos, float deltaTime);

    /**
     * @brief Draw grass (indirect draw)
     * 
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     */
    void draw(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Set grass parameters
     */
    void setParams(const GrassParams& params) { params_ = params; }
    GrassParams& getParams() { return params_; }
    const GrassParams& getParams() const { return params_; }

    /**
     * @brief Enable/disable grass rendering
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    /**
     * @brief Set wind parameters
     */
    void setWind(const glm::vec3& direction, float speed, float strength);

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    jupiter::rendering::RenderGlobals* renderGlobals_ = nullptr;

    // Grass buffers (GPU-only, pointers to avoid incomplete type in header)
    jupiter::rendering::vulkan::VulkanBuffer* instanceBuffer_;
    jupiter::rendering::vulkan::VulkanBuffer* indirectBuffer_;
    jupiter::rendering::vulkan::VulkanBuffer* counterBuffer_;

    // Compute pipeline (generation)
    jupiter::rendering::vulkan::VulkanComputePipeline genPipeline_;
    VkDescriptorPool genDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout genDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet genDescriptorSet_ = VK_NULL_HANDLE;

    // Graphics pipeline (drawing)
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool graphicsDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout graphicsSetLayout1_ = VK_NULL_HANDLE;  // Set 1: instances
    VkDescriptorSetLayout graphicsSetLayout2_ = VK_NULL_HANDLE;  // Set 2: trail textures
    VkDescriptorSet graphicsDescriptorSet1_ = VK_NULL_HANDLE;
    VkDescriptorSet graphicsDescriptorSet2_ = VK_NULL_HANDLE;

    // External references
    VkImageView heightmapView_ = VK_NULL_HANDLE;
    VkSampler heightmapSampler_ = VK_NULL_HANDLE;
    float terrainSize_ = 1024.0f;
    TrailField* trailField_ = nullptr;

    // Parameters
    GrassParams params_;
    bool enabled_ = true;
    float totalTime_ = 0.0f;
    
    // Wind
    glm::vec3 windDirection_ = glm::vec3(1.0f, 0.0f, 0.3f);
    float windSpeed_ = 0.5f;
    float windStrength_ = 0.3f;

    // Shader modules
    VkShaderModule vertShaderModule_ = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule_ = VK_NULL_HANDLE;

    bool createBuffers();
    bool createGenDescriptors();
    bool createGenPipeline();
    bool createGraphicsDescriptors();
    bool createGraphicsPipeline();
    void updateGenDescriptors();
    void updateGraphicsDescriptors();
    VkShaderModule loadShader(const std::string& filepath);
};

} // namespace landscape

