#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <cstdint>

// Forward declare ImageAsset
namespace jupiter {
namespace assets {
    struct ImageAsset;
}
}

namespace jupiter {
namespace rendering {

/**
 * @brief Vulkan texture wrapper with VMA integration
 *
 * Handles texture loading, staging, and GPU memory management.
 * Following CLAUDE.md: All allocations happen during initialization, not runtime.
 */
class VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() { destroy(); }

    // Non-copyable, movable
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&& other) noexcept;
    VulkanTexture& operator=(VulkanTexture&& other) noexcept;

    /**
     * @brief Create texture from raw pixel data
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for transfer commands
     * @param graphicsQueue Graphics queue for transfer operations
     * @param pixels Raw pixel data (RGBA8 format expected)
     * @param width Texture width in pixels
     * @param height Texture height in pixels
     * @param format Vulkan format (default RGBA8 sRGB)
     * @param generateMipmaps Whether to generate mipmap chain
     * @return true if successful
     */
    bool create(VkDevice device, VmaAllocator allocator,
                VkCommandPool commandPool, VkQueue graphicsQueue,
                const void* pixels, uint32_t width, uint32_t height,
                VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                bool generateMipmaps = true);

    /**
     * @brief Create texture from file
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for transfer commands
     * @param graphicsQueue Graphics queue for transfer operations
     * @param filepath Path to image file (PNG, JPG, etc.)
     * @param generateMipmaps Whether to generate mipmap chain
     * @return true if successful
     */
    bool createFromFile(VkDevice device, VmaAllocator allocator,
                       VkCommandPool commandPool, VkQueue graphicsQueue,
                       const std::string& filepath, bool generateMipmaps = true);

    /**
     * @brief Create texture from ImageAsset (CPU asset data)
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for transfer commands
     * @param graphicsQueue Graphics queue for transfer operations
     * @param imageAsset CPU image asset with pixel data
     * @param generateMipmaps Whether to generate mipmap chain
     * @return true if successful
     */
    bool createFromAsset(VkDevice device, VmaAllocator allocator,
                        VkCommandPool commandPool, VkQueue graphicsQueue,
                        const assets::ImageAsset& imageAsset,
                        bool generateMipmaps = true);

    /**
     * @brief Create cubemap texture from 6 faces
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for transfer commands
     * @param graphicsQueue Graphics queue for transfer operations
     * @param facePixels Array of 6 face pixel data pointers (+X, -X, +Y, -Y, +Z, -Z)
     * @param faceSize Size of each face (width and height must be equal)
     * @param format Vulkan format
     * @param generateMipmaps Whether to generate mipmap chain
     * @return true if successful
     */
    bool createCubemap(VkDevice device, VmaAllocator allocator,
                      VkCommandPool commandPool, VkQueue graphicsQueue,
                      const void* const facePixels[6], uint32_t faceSize,
                      VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT,
                      bool generateMipmaps = true);

    /**
     * @brief Create empty cubemap texture (for use as render target)
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param faceSize Size of each face (width and height must be equal)
     * @param format Vulkan format
     * @param generateMipmaps Whether to allocate mipmap chain
     * @param usage Additional usage flags
     * @return true if successful
     */
    bool createCubemapEmpty(VkDevice device, VmaAllocator allocator,
                           uint32_t faceSize, VkFormat format,
                           bool generateMipmaps, VkImageUsageFlags usage);

    /**
     * @brief Create sampler for this texture
     * @param device Vulkan device
     * @param filter Filter mode (VK_FILTER_LINEAR or VK_FILTER_NEAREST)
     * @param addressMode Address mode for texture wrapping
     * @param anisotropyEnable Enable anisotropic filtering
     * @param maxAnisotropy Max anisotropy value (ignored if anisotropyEnable is false)
     * @return true if successful
     */
    bool createSampler(VkDevice device, VkFilter filter = VK_FILTER_LINEAR,
                      VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                      bool anisotropyEnable = true, float maxAnisotropy = 16.0f);

    void destroy();

    // Getters
    VkImage getImage() const { return image_; }
    VkImageView getImageView() const { return imageView_; }
    VkSampler getSampler() const { return sampler_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint32_t getMipLevels() const { return mipLevels_; }
    VkFormat getFormat() const { return format_; }
    bool isCubemap() const { return isCubemap_; }
    uint32_t getLayerCount() const { return layerCount_; }

    bool isValid() const { return image_ != VK_NULL_HANDLE && imageView_ != VK_NULL_HANDLE; }

    // Bindless texture index support
    uint32_t getBindlessIndex() const { return bindlessIndex_; }
    void setBindlessIndex(uint32_t index) { bindlessIndex_ = index; }
    bool hasBindlessIndex() const { return bindlessIndex_ != UINT32_MAX; }

    /**
     * @brief Execute a single-time command buffer
     */
    VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
    void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool,
                              VkQueue graphicsQueue, VkCommandBuffer commandBuffer);

    /**
     * @brief Set image data directly (for compute shader outputs)
     *
     * Used when image is created outside normal flow (e.g., compute shader storage).
     * Takes ownership of the provided Vulkan resources.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param image Vulkan image handle
     * @param imageView Vulkan image view handle
     * @param allocation VMA allocation
     * @param width Image width
     * @param height Image height
     * @param mipLevels Number of mip levels
     * @param layerCount Number of array layers
     * @param format Vulkan format
     */
    void setImageData(VkDevice device, VmaAllocator allocator,
                     VkImage image, VkImageView imageView, VmaAllocation allocation,
                     uint32_t width, uint32_t height, uint32_t mipLevels, uint32_t layerCount,
                     VkFormat format);

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mipLevels_ = 1;
    uint32_t layerCount_ = 1;  // 1 for 2D, 6 for cubemap
    VkFormat format_ = VK_FORMAT_R8G8B8A8_SRGB;
    bool isCubemap_ = false;
    uint32_t bindlessIndex_ = UINT32_MAX;  // Index in bindless texture array

    /**
     * @brief Transition image layout using pipeline barriers
     */
    void transitionImageLayout(VkCommandBuffer commandBuffer,
                              VkImageLayout oldLayout, VkImageLayout newLayout);

    /**
     * @brief Copy buffer to image
     */
    void copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer);

    /**
     * @brief Generate mipmap chain
     */
    bool generateMipmaps(VkCommandBuffer commandBuffer);
};

} // namespace rendering
} // namespace jupiter
