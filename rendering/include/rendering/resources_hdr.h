#pragma once

/**
 * @file resources_hdr.h
 * @brief HDR framebuffer resources for tonemapping
 * 
 * Provides an HDR color attachment for rendering before tonemapping
 * to the final LDR swapchain image.
 */

#include "resources_base.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

/**
 * @brief HDR framebuffer configuration
 */
struct HDRConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;  // 16-bit float HDR
};

/**
 * @brief HDR framebuffer resources
 * 
 * Manages:
 * - HDR color attachment (16-bit float)
 * - Render pass for HDR rendering
 * - Framebuffer for HDR output
 */
class ResourcesHDR : public ResourcesBase {
public:
    ResourcesHDR() = default;
    ~ResourcesHDR() override;

    /**
     * @brief Initialize HDR resources
     */
    void create(VkDevice device,
                VkPhysicalDevice physicalDevice,
                const HDRConfig& config,
                VkImageView depthView);  // Shared depth buffer

    /**
     * @brief Destroy all resources
     */
    void destroy() override;

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height) override;

    /**
     * @brief Get resource name
     */
    const char* getName() const override { return "ResourcesHDR"; }

    /**
     * @brief Check if resources are valid
     */
    bool isValid() const override { return hdrImage_.valid(); }

    // ========================================================================
    // Resource Accessors
    // ========================================================================

    GPUImage& getHDRImage() { return hdrImage_; }
    const GPUImage& getHDRImage() const { return hdrImage_; }

    VkSampler getSampler() const { return sampler_; }
    VkRenderPass getRenderPass() const { return renderPass_; }
    VkFramebuffer getFramebuffer() const { return framebuffer_; }

    const HDRConfig& getConfig() const { return config_; }

    /**
     * @brief Get descriptor info for HDR texture sampling
     */
    VkDescriptorImageInfo getHDRDescriptor() const {
        return hdrImage_.getDescriptorInfo(sampler_);
    }

    /**
     * @brief Update depth view after resize
     */
    void setDepthView(VkImageView depthView);

private:
    void createHDRImage();
    void createSampler();
    void createRenderPass();
    void createFramebuffer();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    HDRConfig config_;

    GPUImage hdrImage_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkImageView depthView_ = VK_NULL_HANDLE;  // External, not owned
};

} // namespace jupiter::rendering

