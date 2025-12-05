#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <array>
#include <vector>

namespace jupiter {
namespace rendering {

/**
 * @brief Configuration for auto-exposure system
 */
struct ExposureConfig {
    float minExposure = 0.1f;           ///< Minimum allowed exposure
    float maxExposure = 10.0f;          ///< Maximum allowed exposure
    float adaptationSpeed = 1.5f;       ///< How fast eye adapts (higher = faster)
    float targetLuminance = 0.18f;      ///< Middle gray target (key value)
    float histogramLowPercent = 0.05f;  ///< Ignore darkest N% of pixels
    float histogramHighPercent = 0.95f; ///< Ignore brightest N% of pixels
    float minLogLuminance = -10.0f;     ///< Min log luminance for histogram range
    float maxLogLuminance = 2.0f;       ///< Max log luminance for histogram range
};

/**
 * @brief GPU buffer for exposure histogram and results
 * 
 * Layout:
 * - histogram[256]: uint32_t bins for log luminance distribution
 * - averageLuminance: float average scene luminance
 * - currentExposure: float current exposure value
 * - targetExposure: float target exposure value
 */
struct ExposureBufferData {
    static constexpr uint32_t HISTOGRAM_BINS = 256;
    
    uint32_t histogram[HISTOGRAM_BINS];
    float averageLuminance;
    float currentExposure;
    float targetExposure;
    float _padding;
};

/**
 * @brief Resources for auto-exposure system
 * 
 * Manages GPU buffers for:
 * - Luminance histogram (256 bins)
 * - Exposure values (current, target, average)
 */
class ResourcesExposure {
public:
    ResourcesExposure() = default;
    ~ResourcesExposure();

    // No copy, allow move
    ResourcesExposure(const ResourcesExposure&) = delete;
    ResourcesExposure& operator=(const ResourcesExposure&) = delete;
    ResourcesExposure(ResourcesExposure&&) noexcept;
    ResourcesExposure& operator=(ResourcesExposure&&) noexcept;

    /**
     * @brief Create exposure resources
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param config Exposure configuration
     * @param framesInFlight Number of frames in flight
     */
    void create(VkDevice device, VmaAllocator allocator, 
                const ExposureConfig& config, uint32_t framesInFlight);

    /**
     * @brief Destroy all resources
     */
    void destroy();

    /**
     * @brief Reset histogram for new frame
     * @param frameIndex Current frame index
     */
    void resetHistogram(uint32_t frameIndex);

    /**
     * @brief Get buffer for a specific frame
     */
    VkBuffer getBuffer(uint32_t frameIndex) const {
        return buffers_[frameIndex % buffers_.size()];
    }

    /**
     * @brief Get descriptor buffer info for binding
     */
    VkDescriptorBufferInfo getDescriptorInfo(uint32_t frameIndex) const;

    /**
     * @brief Get current exposure value (reads from CPU-visible buffer)
     */
    float getCurrentExposure(uint32_t frameIndex) const;

    /**
     * @brief Get configuration
     */
    const ExposureConfig& getConfig() const { return config_; }

    /**
     * @brief Update configuration
     */
    void setConfig(const ExposureConfig& config) { config_ = config; }

    /**
     * @brief Check if resources are initialized
     */
    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    ExposureConfig config_;

    // Per-frame buffers (for double/triple buffering)
    std::vector<VkBuffer> buffers_;
    std::vector<VmaAllocation> allocations_;
    std::vector<VmaAllocationInfo> allocationInfos_;

    // Initial exposure value (used for first frame)
    static constexpr float INITIAL_EXPOSURE = 1.0f;
};

} // namespace rendering
} // namespace jupiter

