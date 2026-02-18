#pragma once

#include "resources_exposure.h"
#include <vulkan/vulkan.h>
#include <string>
#include <array>
#include <vector>

namespace jupiter {
namespace rendering {

// Forward declarations
class ResourcesHDR;

/**
 * @brief Push constants for luminance histogram shader
 */
struct HistogramPushConstants {
    float minLogLuminance;
    float maxLogLuminance;
    float invLogLuminanceRange;
    uint32_t pixelCount;
};

/**
 * @brief Push constants for histogram average shader
 */
struct AveragePushConstants {
    float minLogLuminance;
    float maxLogLuminance;
    float lowPercent;
    float highPercent;
    float targetLuminance;
    uint32_t totalPixels;
    float minExposure;
    float maxExposure;
};

/**
 * @brief Push constants for exposure adaptation shader
 */
struct AdaptPushConstants {
    float deltaTime;
    float adaptSpeedUp;
    float adaptSpeedDown;
    float minExposure;
    float maxExposure;
};

/**
 * @brief Compute pipeline for auto-exposure
 * 
 * Manages three compute pipelines:
 * 1. Luminance histogram - builds histogram from HDR image
 * 2. Histogram average - calculates weighted average luminance
 * 3. Exposure adaptation - smoothly adapts exposure over time
 */
class PipelineExposure {
public:
    PipelineExposure() = default;
    ~PipelineExposure();

    // No copy, allow move
    PipelineExposure(const PipelineExposure&) = delete;
    PipelineExposure& operator=(const PipelineExposure&) = delete;
    PipelineExposure(PipelineExposure&&) noexcept;
    PipelineExposure& operator=(PipelineExposure&&) noexcept;

    /**
     * @brief Create the exposure pipeline
     * @param device Vulkan device
     * @param resources Exposure resources (buffers)
     * @param shaderPath Path to compiled shader directory
     */
    void create(VkDevice device, ResourcesExposure* resources,
                const std::string& shaderPath = "shaders/exposure/");

    /**
     * @brief Destroy all pipeline resources
     */
    void destroy();

    /**
     * @brief Update HDR image binding (call when HDR buffer changes)
     * @param hdrImageView HDR framebuffer image view
     * @param hdrSampler Sampler for HDR image
     */
    void updateHDRBinding(VkImageView hdrImageView, VkSampler hdrSampler);

    /**
     * @brief Record commands to build luminance histogram
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     * @param imageWidth HDR image width
     * @param imageHeight HDR image height
     */
    void recordHistogramPass(VkCommandBuffer cmd, uint32_t frameIndex,
                            uint32_t imageWidth, uint32_t imageHeight);

    /**
     * @brief Record commands to calculate average luminance
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     */
    void recordAveragePass(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Record commands to adapt exposure
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     * @param deltaTime Time since last frame
     */
    void recordAdaptPass(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime);

    /**
     * @brief Execute full exposure calculation
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     * @param imageWidth HDR image width
     * @param imageHeight HDR image height
     * @param deltaTime Time since last frame
     */
    void execute(VkCommandBuffer cmd, uint32_t frameIndex,
                uint32_t imageWidth, uint32_t imageHeight, float deltaTime);

    /**
     * @brief Check if pipeline is initialized
     */
    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

    /**
     * @brief Get current exposure value
     */
    float getCurrentExposure(uint32_t frameIndex) const;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    ResourcesExposure* resources_ = nullptr;

    // Descriptor set layout and pool
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Pipeline layouts (one per shader due to different push constants)
    VkPipelineLayout histogramPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout averagePipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout adaptPipelineLayout_ = VK_NULL_HANDLE;

    // Compute pipelines
    VkPipeline histogramPipeline_ = VK_NULL_HANDLE;
    VkPipeline averagePipeline_ = VK_NULL_HANDLE;
    VkPipeline adaptPipeline_ = VK_NULL_HANDLE;

    // HDR image binding
    VkImageView hdrImageView_ = VK_NULL_HANDLE;
    VkSampler hdrSampler_ = VK_NULL_HANDLE;
    bool hdrBindingDirty_ = true;

    // Helper methods
    bool createDescriptorSetLayout();
    bool createDescriptorPool(uint32_t framesInFlight);
    bool createDescriptorSets(uint32_t framesInFlight);
    bool createPipelineLayouts();
    bool createPipelines(const std::string& shaderPath);
    VkShaderModule loadShaderModule(const std::string& path);
    void updateDescriptorSets();
};

} // namespace rendering
} // namespace jupiter









