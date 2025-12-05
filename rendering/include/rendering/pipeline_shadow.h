#pragma once

/**
 * @file pipeline_shadow.h
 * @brief Shadow map generation pipeline
 * 
 * Renders scene geometry from the light's point of view to generate
 * a depth map for shadow calculations.
 */

#include "pipeline_base.h"
#include "resources_shadow.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

// Forward declarations
class SceneManager;
class VulkanMesh;

/**
 * @brief Push constants for shadow pass
 */
struct ShadowPushConstants {
    alignas(16) glm::mat4 model;  // Model matrix
};

/**
 * @brief Shadow map generation pipeline
 * 
 * Depth-only rendering pass from the light's perspective.
 * Outputs to ResourcesShadow's shadow map.
 */
class PipelineShadow : public PipelineBase {
public:
    /**
     * @brief Create shadow pipeline
     * 
     * @param device Vulkan device
     * @param config Pipeline configuration
     * @param resourcesShadow Shadow resources (for render pass/framebuffer)
     */
    PipelineShadow(VkDevice device,
                   VkPhysicalDevice physicalDevice,
                   const PipelineConfig& config,
                   ResourcesShadow* resourcesShadow);

    ~PipelineShadow() override;

    /**
     * @brief Record shadow pass commands
     * 
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;

    /**
     * @brief Handle window resize (shadow map is resolution-independent)
     */
    void onWindowResized(uint32_t width, uint32_t height) override {}

    /**
     * @brief Shadow pass doesn't use camera UBO directly
     */
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override {}

    /**
     * @brief Set scene manager for renderable access
     */
    void setSceneManager(SceneManager* scene) { sceneManager_ = scene; }

    /**
     * @brief Update shadow parameters before rendering
     * 
     * @param lightPos Light position
     * @param lightDir Light direction
     * @param targetPos Point light looks at
     */
    void updateShadow(const glm::vec3& lightPos,
                      const glm::vec3& lightDir,
                      const glm::vec3& targetPos);

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipeline();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ResourcesShadow* resourcesShadow_ = nullptr;
    SceneManager* sceneManager_ = nullptr;

    std::vector<VkDescriptorSet> shadowDescriptorSets_;
};

} // namespace jupiter::rendering

