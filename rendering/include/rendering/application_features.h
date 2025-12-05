#pragma once

/**
 * @file application_features.h
 * @brief Advanced rendering features integration for Application
 * 
 * Manages optional rendering features:
 * - Shadow mapping
 * - SSAO
 * - HDR tonemapping
 * - Skybox
 * 
 * Features are created and destroyed based on RenderFeatures flags.
 */

#include "render_features.h"
#include "resources_shadow.h"
#include "resources_gbuffer.h"
#include "resources_hdr.h"
#include "pipeline_shadow.h"
#include "pipeline_gbuffer.h"
#include "pipeline_ssao.h"
#include "pipeline_tonemap.h"
#include "pipeline_skybox.h"
#include <memory>

namespace jupiter::rendering {

// Forward declarations
class SceneManager;
class VulkanTexture;

/**
 * @brief Manager for advanced rendering features
 * 
 * Creates and manages optional rendering pipelines based on feature flags.
 * Integrates with existing Application/SceneManager infrastructure.
 */
class ApplicationFeatures {
public:
    ApplicationFeatures() = default;
    ~ApplicationFeatures();

    /**
     * @brief Initialize features based on enabled flags
     * 
     * @param device Vulkan device
     * @param physicalDevice Physical device
     * @param swapchainRenderPass Main render pass
     * @param width Swapchain width
     * @param height Swapchain height
     * @param framesInFlight Number of frames in flight
     */
    void initialize(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkRenderPass swapchainRenderPass,
                    VkImageView depthImageView,
                    uint32_t width,
                    uint32_t height,
                    uint32_t framesInFlight);

    /**
     * @brief Destroy all feature resources
     */
    void destroy();

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height, VkImageView newDepthView);

    /**
     * @brief Get/set render features configuration
     */
    RenderFeatures& features() { return features_; }
    const RenderFeatures& features() const { return features_; }

    /**
     * @brief Enable a feature (creates resources if needed)
     */
    void enableFeature(RenderFeature feature);

    /**
     * @brief Disable a feature (destroys resources)
     */
    void disableFeature(RenderFeature feature);

    /**
     * @brief Set scene manager for rendering
     */
    void setSceneManager(SceneManager* sceneManager);

    /**
     * @brief Set environment cubemap for skybox
     */
    void setEnvironmentMap(VulkanTexture* envCubemap);

    // ========================================================================
    // Rendering Interface
    // ========================================================================

    /**
     * @brief Update camera UBOs for all active pipelines
     */
    void updateCameraUBO(const CameraUBO& ubo, uint32_t frameIndex);

    /**
     * @brief Update shadow light parameters
     * 
     * @param lightPos Light position (for directional, use far position)
     * @param lightDir Light direction
     * @param targetPos Point the light looks at
     */
    void updateShadowLight(const glm::vec3& lightPos,
                          const glm::vec3& lightDir,
                          const glm::vec3& targetPos);

    /**
     * @brief Record shadow pass commands
     */
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Record G-buffer pass commands
     */
    void recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Record SSAO pass commands
     */
    void recordSSAOPass(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Record skybox commands (call within main render pass)
     */
    void recordSkybox(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Record tonemap pass commands (call within final swapchain render pass)
     */
    void recordTonemapPass(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Check if specific feature is currently active
     */
    bool isFeatureActive(RenderFeature feature) const;

    // ========================================================================
    // Resource Accessors (for PBR shader binding)
    // ========================================================================

    /**
     * @brief Get shadow map descriptor (for binding in PBR pass)
     */
    VkDescriptorImageInfo getShadowMapDescriptor() const;

    /**
     * @brief Get SSAO texture descriptor (for binding in PBR pass)
     */
    VkDescriptorImageInfo getSSAODescriptor() const;

    /**
     * @brief Get HDR framebuffer for rendering
     */
    ResourcesHDR* getHDRResources() { return resourcesHDR_.get(); }

    /**
     * @brief Get shadow light-space matrix
     */
    glm::mat4 getLightSpaceMatrix() const;

private:
    void createShadowResources();
    void createSSAOResources();
    void createHDRResources();
    void createSkyboxPipeline();

    void destroyShadowResources();
    void destroySSAOResources();
    void destroyHDRResources();
    void destroySkyboxPipeline();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t framesInFlight_ = 2;

    RenderFeatures features_;
    SceneManager* sceneManager_ = nullptr;
    VulkanTexture* envCubemap_ = nullptr;

    // Shadow mapping
    std::unique_ptr<ResourcesShadow> resourcesShadow_;
    std::unique_ptr<PipelineShadow> pipelineShadow_;

    // SSAO
    std::unique_ptr<ResourcesGBuffer> resourcesGBuffer_;
    std::unique_ptr<PipelineGBuffer> pipelineGBuffer_;
    std::unique_ptr<PipelineSSAO> pipelineSSAO_;

    // HDR / Tonemap
    std::unique_ptr<ResourcesHDR> resourcesHDR_;
    std::unique_ptr<PipelineTonemap> pipelineTonemap_;

    // Skybox
    std::unique_ptr<PipelineSkybox> pipelineSkybox_;

    bool initialized_ = false;
};

} // namespace jupiter::rendering

