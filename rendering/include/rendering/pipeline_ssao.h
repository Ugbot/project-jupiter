#pragma once

/**
 * @file pipeline_ssao.h
 * @brief Screen-Space Ambient Occlusion (SSAO) pipeline
 * 
 * Calculates ambient occlusion from G-buffer data using
 * hemisphere kernel sampling.
 */

#include "pipeline_base.h"
#include "resources_gbuffer.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

/**
 * @brief SSAO UBO structure
 */
struct SSAOUBO {
    alignas(16) glm::mat4 projection;
    alignas(4)  float radius;
    alignas(4)  float bias;
    alignas(4)  float power;
    alignas(4)  float screenWidth;
    alignas(4)  float screenHeight;
    alignas(4)  float noiseSize;
    alignas(8)  float padding[2];
};

/**
 * @brief SSAO configuration
 */
struct SSAOParams {
    float radius = 0.5f;   // Sample radius in view space
    float bias = 0.025f;   // Depth bias to prevent self-occlusion
    float power = 2.0f;    // Occlusion power (contrast)
};

/**
 * @brief SSAO pipeline
 * 
 * Samples G-buffer position/normal to calculate screen-space
 * ambient occlusion using a hemisphere kernel.
 */
class PipelineSSAO : public PipelineBase {
public:
    /**
     * @brief Create SSAO pipeline
     */
    PipelineSSAO(VkDevice device,
                 VkPhysicalDevice physicalDevice,
                 const PipelineConfig& config,
                 ResourcesGBuffer* resourcesGBuffer);

    ~PipelineSSAO() override;

    /**
     * @brief Record SSAO pass commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height) override;

    /**
     * @brief Update camera UBO for projection matrix
     */
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override;

    /**
     * @brief Set SSAO parameters
     */
    void setParameters(float radius, float bias, float power) {
        params_.radius = radius;
        params_.bias = bias;
        params_.power = power;
    }

    SSAOParams& getParams() { return params_; }
    const SSAOParams& getParams() const { return params_; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUBOBuffers();
    void createPipeline();
    void updateDescriptorSets();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ResourcesGBuffer* resourcesGBuffer_ = nullptr;

    SSAOParams params_;
    std::vector<VkDescriptorSet> ssaoDescriptorSets_;
    std::vector<GPUBuffer> ssaoUBOs_;
};

} // namespace jupiter::rendering

