#pragma once

/**
 * @file resources_base.h
 * @brief Base class for shared GPU resources (HelloVulkan-inspired)
 * 
 * Resources are shared data structures (buffers, images) that can be
 * accessed by multiple pipelines. Examples: light buffers, shadow maps,
 * clustered forward data structures.
 */

#include <vulkan/vulkan.h>
#include <string>

namespace jupiter::rendering {

// Forward declaration
class VulkanContext;

/**
 * @brief Base class for shared GPU resources
 * 
 * Derived classes manage specific resource types:
 * - ResourcesLight: Light SSBO and shadow maps
 * - ResourcesClusterForward: Cluster AABBs, light cells, indices
 * - ResourcesIBL: Environment maps, BRDF LUT
 */
class ResourcesBase {
public:
    virtual ~ResourcesBase() = default;

    /**
     * @brief Destroy all GPU resources
     * 
     * Called before device destruction or when recreating resources.
     */
    virtual void destroy() = 0;

    /**
     * @brief Handle window resize
     * 
     * Called when swapchain is recreated. Override if resources
     * are resolution-dependent (e.g., G-buffer).
     */
    virtual void onWindowResized(uint32_t width, uint32_t height) {}

    /**
     * @brief Get resource name for debugging
     */
    virtual const char* getName() const { return "ResourcesBase"; }

    /**
     * @brief Check if resources are initialized and valid
     */
    virtual bool isValid() const { return false; }
};

/**
 * @brief RAII wrapper for Vulkan buffer with VMA
 */
struct GPUBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;  // Or VmaAllocation
    VkDeviceSize size = 0;
    void* mappedData = nullptr;

    VkDescriptorBufferInfo getDescriptorInfo() const {
        return {
            .buffer = buffer,
            .offset = 0,
            .range = size
        };
    }

    bool valid() const { return buffer != VK_NULL_HANDLE; }
};

/**
 * @brief RAII wrapper for Vulkan image
 */
struct GPUImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    uint32_t mipLevels = 1;

    VkDescriptorImageInfo getDescriptorInfo(VkSampler sampler, 
                                            VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) const {
        return {
            .sampler = sampler,
            .imageView = view,
            .imageLayout = layout
        };
    }

    bool valid() const { return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE; }
};

} // namespace jupiter::rendering

