#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <vector>
#include <memory>

namespace jupiter {
namespace rendering {

class VulkanTexture;

/**
 * @brief Configuration for IBL generation
 */
struct IBLConfig {
    // Reduced resolution for faster generation (avoids GPU timeout)
    static constexpr uint32_t InputCubeSideLength = 512;   // Was 1024
    static constexpr uint32_t OutputDiffuseSideLength = 32;
    static constexpr uint32_t OutputSpecularSideLength = 64; // Was 128
    static constexpr uint32_t OutputDiffuseSampleCount = 512; // Was 1024 (reduced to avoid GPU timeout)
    static constexpr uint32_t LayerCount = 6;  // Cubemap has 6 faces
    static constexpr VkFormat CubeFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    
    // BRDF LUT
    static constexpr uint32_t LUTWidth = 256;
    static constexpr uint32_t LUTHeight = 256;
    static constexpr uint32_t LUTSampleCount = 512; // Was 1024
};

/**
 * @brief Push constants for cubemap filtering
 */
struct PushConstCubeFilter {
    float roughness = 0.0f;
    uint32_t sampleCount = 1024;
};

/**
 * @brief IBL Pipeline for generating IBL resources from HDR environment maps
 * 
 * Implements the complete IBL generation pipeline:
 * 1. Equirectangular HDR -> Cubemap conversion
 * 2. Diffuse irradiance filtering (cosine-weighted hemisphere convolution)
 * 3. Specular prefiltering (GGX importance sampling with roughness mipmaps)
 */
class PipelineIBL {
public:
    PipelineIBL() = default;
    ~PipelineIBL() { destroy(); }
    
    // Non-copyable
    PipelineIBL(const PipelineIBL&) = delete;
    PipelineIBL& operator=(const PipelineIBL&) = delete;
    
    /**
     * @brief Initialize the IBL pipeline
     */
    bool create(VkDevice device, VmaAllocator allocator,
                VkCommandPool commandPool, VkQueue queue);
    
    /**
     * @brief Destroy all resources
     */
    void destroy();
    
    /**
     * @brief Load HDR image and create environment cubemap
     * @param hdrFilePath Path to HDR equirectangular image
     * @param outputCubemap Output environment cubemap (1024x1024 per face)
     * @return true if successful
     */
    bool convertEquirectToCubemap(const std::string& hdrFilePath,
                                  VulkanTexture* outputCubemap);
    
    /**
     * @brief Generate diffuse irradiance map from environment cubemap
     * @param inputCubemap Source environment cubemap
     * @param outputIrradiance Output irradiance cubemap (32x32 per face)
     * @return true if successful
     */
    bool filterDiffuseIrradiance(VulkanTexture* inputCubemap,
                                  VulkanTexture* outputIrradiance);
    
    /**
     * @brief Generate specular prefiltered map with roughness mipmaps
     * @param inputCubemap Source environment cubemap
     * @param outputPrefiltered Output prefiltered cubemap (128x128 per face, mipmapped)
     * @return true if successful
     */
    bool filterSpecularPrefiltered(VulkanTexture* inputCubemap,
                                    VulkanTexture* outputPrefiltered);
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    
    // Render pass for cubemap rendering (6 MRT attachments)
    VkRenderPass cubemapRenderPass_ = VK_NULL_HANDLE;
    
    // Pipelines
    VkPipeline equirectToCubePipeline_ = VK_NULL_HANDLE;
    VkPipeline diffuseFilterPipeline_ = VK_NULL_HANDLE;
    VkPipeline specularFilterPipeline_ = VK_NULL_HANDLE;
    
    // Pipeline layouts
    VkPipelineLayout equirectLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout filterLayout_ = VK_NULL_HANDLE;  // With push constants
    
    // Descriptor set layouts
    VkDescriptorSetLayout equirectDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout filterDescLayout_ = VK_NULL_HANDLE;
    
    // Descriptor pool
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // Temporary HDR texture for equirect conversion
    std::unique_ptr<VulkanTexture> hdrTexture_;
    
    // Helper functions
    bool createCubemapRenderPass();
    bool createEquirectPipeline();
    bool createFilterPipelines();
    
    bool createCubemapImage(VulkanTexture* cubemap, uint32_t size, uint32_t mipLevels);
    bool createCubemapFramebuffer(VulkanTexture* cubemap, uint32_t size, uint32_t mipLevel,
                                   VkFramebuffer& framebuffer, std::vector<VkImageView>& faceViews);
    
    void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t baseMip, uint32_t mipCount,
                               uint32_t baseLayer, uint32_t layerCount);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    uint32_t calculateMipLevels(uint32_t size);

    // Mipmap generation for cubemaps (separate command buffer submission)
    void generateCubemapMipmaps(VulkanTexture* cubemap, uint32_t mipLevels);
};

} // namespace rendering
} // namespace jupiter

