#pragma once

/**
 * @file resources_shadow.h
 * @brief Shadow mapping resources (shadow map, light-space matrix)
 * 
 * Provides GPU resources for shadow mapping:
 * - Depth-only shadow map texture
 * - Light-space transformation matrix
 * - Shadow calculation parameters (bias)
 */

#include "resources_base.h"
#include <glm/glm.hpp>
#include <vector>

namespace jupiter::rendering {

/**
 * @brief Shadow map uniform buffer object
 */
struct ShadowMapUBO {
    alignas(16) glm::mat4 lightSpaceMatrix;  // View-projection from light's POV
    alignas(16) glm::vec4 lightPosition;     // Light position (xyz) + type (w: 0=directional, 1=point)
    alignas(4)  float shadowMinBias;         // Minimum shadow bias
    alignas(4)  float shadowMaxBias;         // Maximum shadow bias (slope-scaled)
    alignas(4)  float shadowNearPlane;       // Shadow frustum near
    alignas(4)  float shadowFarPlane;        // Shadow frustum far
};

/**
 * @brief Shadow mapping configuration
 */
struct ShadowMapConfig {
    uint32_t resolution = 2048;              // Shadow map resolution (square)
    float nearPlane = 0.1f;                  // Light frustum near plane
    float farPlane = 100.0f;                 // Light frustum far plane
    float orthoSize = 20.0f;                 // Orthographic projection size (for directional)
    float minBias = 0.0005f;                 // Minimum shadow bias
    float maxBias = 0.005f;                  // Maximum shadow bias
    bool usePCF = true;                      // Enable PCF soft shadows
    uint32_t pcfKernelSize = 3;              // PCF kernel size (3x3)
};

/**
 * @brief Shadow mapping resources
 * 
 * Manages:
 * - Shadow map depth texture
 * - Shadow map sampler (with comparison)
 * - Per-frame UBOs for light-space matrix
 * - Framebuffer for shadow pass
 */
class ResourcesShadow : public ResourcesBase {
public:
    ResourcesShadow() = default;
    ~ResourcesShadow() override;

    /**
     * @brief Initialize shadow map resources
     * 
     * @param device Vulkan device
     * @param physicalDevice Physical device for memory properties
     * @param config Shadow map configuration
     * @param framesInFlight Number of frames in flight (for UBO count)
     */
    void create(VkDevice device, 
                VkPhysicalDevice physicalDevice,
                const ShadowMapConfig& config,
                uint32_t framesInFlight);

    /**
     * @brief Destroy all resources
     */
    void destroy() override;

    /**
     * @brief Get resource name
     */
    const char* getName() const override { return "ResourcesShadow"; }

    /**
     * @brief Check if resources are valid
     */
    bool isValid() const override { return shadowMap_.valid(); }

    /**
     * @brief Update shadow UBO for a frame
     * 
     * @param frameIndex Current frame index
     * @param lightPos Light position (world space)
     * @param lightDir Light direction (for directional lights)
     * @param targetPos Point the light is looking at
     */
    void updateShadowUBO(uint32_t frameIndex,
                         const glm::vec3& lightPos,
                         const glm::vec3& lightDir,
                         const glm::vec3& targetPos);

    /**
     * @brief Get light-space matrix for shadow coordinate calculation
     */
    const glm::mat4& getLightSpaceMatrix() const { return shadowUBO_.lightSpaceMatrix; }

    // ========================================================================
    // Resource Accessors
    // ========================================================================

    GPUImage& getShadowMap() { return shadowMap_; }
    const GPUImage& getShadowMap() const { return shadowMap_; }

    VkSampler getShadowSampler() const { return shadowSampler_; }
    VkFramebuffer getFramebuffer() const { return framebuffer_; }
    VkRenderPass getRenderPass() const { return renderPass_; }

    GPUBuffer& getUBO(uint32_t frameIndex) { return uboBuffers_[frameIndex]; }
    const GPUBuffer& getUBO(uint32_t frameIndex) const { return uboBuffers_[frameIndex]; }

    const ShadowMapConfig& getConfig() const { return config_; }
    uint32_t getResolution() const { return config_.resolution; }

    /**
     * @brief Get descriptor info for shadow map sampling
     */
    VkDescriptorImageInfo getShadowMapDescriptor() const {
        return shadowMap_.getDescriptorInfo(shadowSampler_, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

private:
    void createShadowMap();
    void createSampler();
    void createRenderPass();
    void createFramebuffer();
    void createUBOBuffers(uint32_t framesInFlight);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ShadowMapConfig config_;

    // Shadow map depth texture
    GPUImage shadowMap_;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;

    // Shadow pass framebuffer and render pass
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    // Per-frame UBOs
    std::vector<GPUBuffer> uboBuffers_;

    // Current shadow UBO data
    ShadowMapUBO shadowUBO_;
};

} // namespace jupiter::rendering

