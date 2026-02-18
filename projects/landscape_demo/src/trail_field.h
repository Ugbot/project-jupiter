#pragma once

#include "rendering/texture.h"
#include "rendering/vulkan_compute_pipeline.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

// Forward declare VulkanBuffer from internal header
namespace jupiter {
namespace rendering {
namespace vulkan {
    class VulkanBuffer;
}
}
}

namespace landscape {

constexpr uint32_t MAX_TRAIL_EVENTS = 32;

/**
 * @brief Trail event for CPU→GPU staging (std430 layout)
 */
struct TrailEvent {
    glm::vec4 pos_radius;   // x=worldX, y=worldZ, z=radiusMeters, w=intensity(0..1)
    glm::vec4 dir_bend;     // x=dirX, y=dirZ, z=bendStrength, w=flattenStrength
};

/**
 * @brief GPU trail field for grass flattening + bending
 * 
 * Manages ping-pong textures (intensity + direction) and compute pipeline
 * for stamping player trails and relaxation over time.
 */
class TrailField {
public:
    TrailField() = default;
    ~TrailField();

    // Non-copyable
    TrailField(const TrailField&) = delete;
    TrailField& operator=(const TrailField&) = delete;

    /**
     * @brief Initialize trail field resources
     * 
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for texture creation
     * @param graphicsQueue Graphics queue for texture creation
     * @param worldSize Size of trail region in meters (default 256m)
     * @param resolution Texture resolution (default 512x512)
     * @return true if successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator,
                    VkCommandPool commandPool, VkQueue graphicsQueue,
                    float worldSize = 256.0f, uint32_t resolution = 512);

    void destroy();

    /**
     * @brief Push a trail event (CPU staging, no alloc)
     */
    void pushEvent(const TrailEvent& event);

    /**
     * @brief Clear all events for this frame
     */
    void clearEvents();

    /**
     * @brief Update trail field (dispatch compute shader)
     * 
     * @param cmd Command buffer
     * @param dtSeconds Delta time in seconds
     * @param newOrigin New world origin (bottom-left of trail region)
     */
    void update(VkCommandBuffer cmd, float dtSeconds, const glm::vec2& newOrigin);

    /**
     * @brief Set relaxation time (5-20s)
     */
    void setRelaxSeconds(float seconds);

    /**
     * @brief Set world size
     */
    void setWorldSize(float meters) { worldSize_ = meters; }

    // Getters
    VkImageView getIntensityView() const { return currentIntensity_->getImageView(); }
    VkImageView getDirView() const { return currentDir_->getImageView(); }
    VkSampler getSampler() const { return intensityA_.getSampler(); }
    float getWorldSize() const { return worldSize_; }
    glm::vec2 getOrigin() const { return currentOrigin_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Ping-pong textures
    jupiter::rendering::VulkanTexture intensityA_;
    jupiter::rendering::VulkanTexture intensityB_;
    jupiter::rendering::VulkanTexture dirA_;
    jupiter::rendering::VulkanTexture dirB_;

    jupiter::rendering::VulkanTexture* currentIntensity_ = nullptr;
    jupiter::rendering::VulkanTexture* currentDir_ = nullptr;
    jupiter::rendering::VulkanTexture* prevIntensity_ = nullptr;
    jupiter::rendering::VulkanTexture* prevDir_ = nullptr;

    // Events SSBO (pointer to avoid incomplete type in header)
    jupiter::rendering::vulkan::VulkanBuffer* eventsSSBO_;
    std::vector<TrailEvent> eventsStaging_;  // CPU side (preallocated)

    // Compute pipeline
    jupiter::rendering::vulkan::VulkanComputePipeline updatePipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;  // ping-pong sets

    // Parameters
    float worldSize_ = 256.0f;
    uint32_t resolution_ = 512;
    float relaxSeconds_ = 12.0f;
    glm::vec2 currentOrigin_ = glm::vec2(0.0f);
    glm::vec2 prevOrigin_ = glm::vec2(0.0f);

    bool createTextures(VkCommandPool commandPool, VkQueue graphicsQueue);
    bool createDescriptors();
    bool createPipeline();
    void updateDescriptorSet(uint32_t setIndex);
};

} // namespace landscape

