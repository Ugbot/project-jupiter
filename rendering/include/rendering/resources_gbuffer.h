#pragma once

/**
 * @file resources_gbuffer.h
 * @brief G-buffer resources for deferred rendering and SSAO
 * 
 * Provides GPU resources for geometry buffer:
 * - View-space position texture
 * - View-space normal texture
 * - SSAO noise texture
 * - SSAO kernel buffer
 * - SSAO output texture
 */

#include "resources_base.h"
#include <glm/glm.hpp>
#include <vector>

namespace jupiter::rendering {

/**
 * @brief G-buffer configuration
 */
struct GBufferConfig {
    uint32_t width = 1920;           // Framebuffer width
    uint32_t height = 1080;          // Framebuffer height
    uint32_t ssaoKernelSize = 64;    // Number of SSAO samples
    uint32_t noiseSize = 4;          // Noise texture dimension (4x4)
};

/**
 * @brief G-buffer resources for SSAO and deferred rendering
 * 
 * Manages:
 * - Position texture (view-space XYZ + linear depth in W)
 * - Normal texture (view-space normals)
 * - Noise texture for SSAO random rotation
 * - SSAO kernel SSBO
 * - SSAO output texture
 */
class ResourcesGBuffer : public ResourcesBase {
public:
    ResourcesGBuffer() = default;
    ~ResourcesGBuffer() override;

    /**
     * @brief Initialize G-buffer resources
     * 
     * @param device Vulkan device
     * @param physicalDevice Physical device for memory properties
     * @param config G-buffer configuration
     * @param framesInFlight Number of frames in flight
     */
    void create(VkDevice device,
                VkPhysicalDevice physicalDevice,
                const GBufferConfig& config,
                uint32_t framesInFlight);

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
    const char* getName() const override { return "ResourcesGBuffer"; }

    /**
     * @brief Check if resources are valid
     */
    bool isValid() const override { return position_.valid() && normal_.valid(); }

    /**
     * @brief Get noise texture dimension
     */
    float getNoiseDimension() const { return static_cast<float>(config_.noiseSize); }

    // ========================================================================
    // Resource Accessors
    // ========================================================================

    GPUImage& getPositionTexture() { return position_; }
    const GPUImage& getPositionTexture() const { return position_; }

    GPUImage& getNormalTexture() { return normal_; }
    const GPUImage& getNormalTexture() const { return normal_; }

    GPUImage& getNoiseTexture() { return noise_; }
    const GPUImage& getNoiseTexture() const { return noise_; }

    GPUImage& getSSAOTexture() { return ssao_; }
    const GPUImage& getSSAOTexture() const { return ssao_; }

    GPUImage& getDepthTexture() { return depth_; }
    const GPUImage& getDepthTexture() const { return depth_; }

    GPUBuffer& getKernelBuffer() { return kernel_; }
    const GPUBuffer& getKernelBuffer() const { return kernel_; }

    VkSampler getSampler() const { return sampler_; }
    VkSampler getNoiseSampler() const { return noiseSampler_; }

    VkRenderPass getGBufferRenderPass() const { return gBufferRenderPass_; }
    VkFramebuffer getGBufferFramebuffer() const { return gBufferFramebuffer_; }

    VkRenderPass getSSAORenderPass() const { return ssaoRenderPass_; }
    VkFramebuffer getSSAOFramebuffer() const { return ssaoFramebuffer_; }

    const GBufferConfig& getConfig() const { return config_; }

private:
    void createPositionTexture();
    void createNormalTexture();
    void createNoiseTexture();
    void createSSAOTexture();
    void createDepthTexture();
    void createKernelBuffer();
    void createSamplers();
    void createGBufferRenderPass();
    void createGBufferFramebuffer();
    void createSSAORenderPass();
    void createSSAOFramebuffer();
    void generateSSAOKernel();
    void generateNoiseTexture();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    GBufferConfig config_;

    // G-buffer textures (view-space)
    GPUImage position_;  // XYZ = view-space pos, W = linear depth (1=foreground, 0=background)
    GPUImage normal_;    // RGB = view-space normal
    GPUImage depth_;     // Depth buffer for G-buffer pass

    // SSAO resources
    GPUImage noise_;     // Random rotation vectors for SSAO
    GPUImage ssao_;      // SSAO output (single channel)
    GPUBuffer kernel_;   // SSAO hemisphere kernel (vec3 array)

    // Samplers
    VkSampler sampler_ = VK_NULL_HANDLE;       // For G-buffer textures
    VkSampler noiseSampler_ = VK_NULL_HANDLE;  // Repeating sampler for noise

    // Render passes and framebuffers
    VkRenderPass gBufferRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer gBufferFramebuffer_ = VK_NULL_HANDLE;
    VkRenderPass ssaoRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer ssaoFramebuffer_ = VK_NULL_HANDLE;

    // SSAO kernel data (CPU side for generation)
    std::vector<glm::vec4> ssaoKernel_;
};

} // namespace jupiter::rendering

