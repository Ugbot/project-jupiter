#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <memory>

// Forward declarations
namespace jupiter {
namespace rendering {
    class VulkanTexture;
}
}

namespace jupiter {
namespace rendering {

/**
 * @brief Image-Based Lighting (IBL) resources manager
 *
 * Manages the 3 GPU textures required for physically-based IBL:
 * 1. Diffuse irradiance cubemap (for diffuse ambient)
 * 2. Specular prefiltered environment cubemap (for reflections)
 * 3. BRDF integration LUT (for split-sum approximation)
 *
 * Can generate these from HDR equirectangular environment maps
 * or create fallback resources for assets without IBL data.
 */
class IBLResources {
public:
    IBLResources() = default;
    ~IBLResources() { destroy(); }

    // Non-copyable, movable
    IBLResources(const IBLResources&) = delete;
    IBLResources& operator=(const IBLResources&) = delete;
    IBLResources(IBLResources&&) noexcept = default;
    IBLResources& operator=(IBLResources&&) noexcept = default;

    /**
     * @brief Generate IBL resources from HDR environment map
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for GPU operations
     * @param queue Graphics queue
     * @param hdrFilepath Path to HDR equirectangular image
     * @return true if successful
     */
    bool generateFromHDR(VkDevice device, VmaAllocator allocator,
                        VkCommandPool commandPool, VkQueue queue,
                        const std::string& hdrFilepath);

    /**
     * @brief Create fallback IBL resources (neutral gray environment)
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for GPU operations
     * @param queue Graphics queue
     * @return true if successful
     */
    bool createFallback(VkDevice device, VmaAllocator allocator,
                       VkCommandPool commandPool, VkQueue queue);

    /**
     * @brief Generate BRDF LUT using compute shader
     * Pre-integrates the split-sum approximation for PBR (Phase 1)
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for GPU operations
     * @param queue Graphics queue
     * @return true if successful
     */
    bool generateBRDFLUT(VkDevice device, VmaAllocator allocator,
                        VkCommandPool commandPool, VkQueue queue);

    /**
     * @brief Destroy all IBL resources
     */
    void destroy();

    /**
     * @brief Check if IBL resources are valid and ready to use
     */
    bool isValid() const;

    // Texture getters for descriptor binding
    VkImageView getIrradianceMapView() const;
    VkImageView getPrefilteredMapView() const;
    VkImageView getBRDFLUTView() const;

    VkSampler getIrradianceMapSampler() const;
    VkSampler getPrefilteredMapSampler() const;
    VkSampler getBRDFLUTSampler() const;

    /**
     * @brief Get max mip levels for specular prefiltered map
     * Used in shader for textureLod(..., roughness * maxLod)
     */
    uint32_t getMaxReflectionLod() const;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // IBL textures (owned)
    std::unique_ptr<VulkanTexture> irradianceMap_;     // Diffuse: 32x32 per face
    std::unique_ptr<VulkanTexture> prefilteredMap_;    // Specular: 128x128 per face, mipmapped
    std::unique_ptr<VulkanTexture> brdfLUT_;           // BRDF LUT: 256x256 2D

    // Optional: environment cubemap for skybox
    std::unique_ptr<VulkanTexture> environmentMap_;    // Optional: 1024x1024 per face

    /**
     * @brief Convert equirectangular HDR to cubemap
     */
    bool convertEquirectToCubemap(VkCommandPool commandPool, VkQueue queue,
                                  VulkanTexture& hdrTexture);

    /**
     * @brief Filter cubemap for diffuse irradiance
     * Convolves environment map with cosine-weighted hemisphere
     */
    bool filterDiffuseIrradiance(VkCommandPool commandPool, VkQueue queue);

    /**
     * @brief Filter cubemap for specular prefiltering
     * GGX importance sampling for different roughness levels (mipmaps)
     */
    bool filterSpecularPrefiltered(VkCommandPool commandPool, VkQueue queue);
};

} // namespace rendering
} // namespace jupiter
