#pragma once

/**
 * @file pipeline_skybox.h
 * @brief Skybox rendering pipeline
 * 
 * Renders environment cubemap as a skybox at infinite distance.
 */

#include "pipeline_base.h"
#include "resources_base.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

// Forward declaration
class VulkanTexture;

/**
 * @brief Skybox rendering pipeline
 * 
 * Renders a fullscreen cube sampling from an environment cubemap.
 * Should be rendered before scene geometry with depth test but no depth write.
 */
class PipelineSkybox : public PipelineBase {
public:
    /**
     * @brief Create skybox pipeline
     */
    PipelineSkybox(VkDevice device,
                   VkPhysicalDevice physicalDevice,
                   const PipelineConfig& config,
                   VkRenderPass renderPass,
                   VulkanTexture* envCubemap);

    ~PipelineSkybox() override;

    /**
     * @brief Record skybox rendering commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height) override;

    /**
     * @brief Update camera UBO (removes translation for infinite distance effect)
     */
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override;

    /**
     * @brief Set environment cubemap
     */
    void setEnvironmentMap(VulkanTexture* envCubemap);

    /**
     * @brief Set viewport dimensions
     */
    void setViewportSize(uint32_t width, uint32_t height) {
        viewportWidth_ = width;
        viewportHeight_ = height;
    }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUBOBuffers();
    void createPipeline(VkRenderPass renderPass);
    void updateDescriptorSets();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VulkanTexture* envCubemap_ = nullptr;

    std::vector<VkDescriptorSet> skyboxDescriptorSets_;
    std::vector<GPUBuffer> cameraUBOs_;

    uint32_t viewportWidth_ = 1920;
    uint32_t viewportHeight_ = 1080;
};

} // namespace jupiter::rendering

