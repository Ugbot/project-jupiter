#pragma once

/**
 * @file pipeline_tonemap.h
 * @brief HDR tonemapping pipeline
 * 
 * Applies tonemapping to HDR framebuffer and outputs to swapchain.
 */

#include "pipeline_base.h"
#include "resources_hdr.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

/**
 * @brief Tonemap operator selection
 */
enum class TonemapOperator {
    ACES,           // ACES filmic (default)
    Reinhard,       // Simple Reinhard
    Uncharted2,     // Uncharted 2 filmic
    Exposure        // Simple exposure adjustment
};

/**
 * @brief Tonemap push constants
 */
struct TonemapPushConstants {
    alignas(4) float exposure;     // Exposure multiplier
    alignas(4) float gamma;        // Gamma correction (usually 2.2)
    alignas(4) int   operator_;    // Tonemap operator selection
    alignas(4) float padding;
};

/**
 * @brief HDR tonemapping pipeline
 * 
 * Renders fullscreen quad sampling HDR texture and applying
 * tonemapping + gamma correction to output LDR image.
 */
class PipelineTonemap : public PipelineBase {
public:
    /**
     * @brief Create tonemap pipeline
     */
    PipelineTonemap(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    const PipelineConfig& config,
                    ResourcesHDR* resourcesHDR,
                    VkRenderPass swapchainRenderPass);

    ~PipelineTonemap() override;

    /**
     * @brief Record tonemap pass commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height) override;

    /**
     * @brief Camera UBO not needed for tonemap
     */
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override {}

    /**
     * @brief Set tonemapping parameters
     */
    void setExposure(float exposure) { exposure_ = exposure; }
    void setGamma(float gamma) { gamma_ = gamma; }
    void setOperator(TonemapOperator op) { operator_ = op; }

    float getExposure() const { return exposure_; }
    float getGamma() const { return gamma_; }
    TonemapOperator getOperator() const { return operator_; }

    /**
     * @brief Set swapchain framebuffer for rendering
     */
    void setSwapchainFramebuffer(VkFramebuffer fb) { currentFramebuffer_ = fb; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipeline();
    void updateDescriptorSets();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ResourcesHDR* resourcesHDR_ = nullptr;
    VkRenderPass swapchainRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer currentFramebuffer_ = VK_NULL_HANDLE;

    float exposure_ = 1.0f;
    float gamma_ = 2.2f;
    TonemapOperator operator_ = TonemapOperator::ACES;

    std::vector<VkDescriptorSet> tonemapDescriptorSets_;
};

} // namespace jupiter::rendering

