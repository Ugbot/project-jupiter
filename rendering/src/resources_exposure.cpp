#include "rendering/resources_exposure.h"
#include "logging/logging.h"
#include <cstring>
#include <algorithm>

namespace jupiter {
namespace rendering {

ResourcesExposure::~ResourcesExposure() {
    destroy();
}

ResourcesExposure::ResourcesExposure(ResourcesExposure&& other) noexcept
    : device_(other.device_)
    , allocator_(other.allocator_)
    , config_(other.config_)
    , buffers_(std::move(other.buffers_))
    , allocations_(std::move(other.allocations_))
    , allocationInfos_(std::move(other.allocationInfos_)) {
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = VK_NULL_HANDLE;
}

ResourcesExposure& ResourcesExposure::operator=(ResourcesExposure&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        allocator_ = other.allocator_;
        config_ = other.config_;
        buffers_ = std::move(other.buffers_);
        allocations_ = std::move(other.allocations_);
        allocationInfos_ = std::move(other.allocationInfos_);
        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
    }
    return *this;
}

void ResourcesExposure::create(VkDevice device, VmaAllocator allocator,
                               const ExposureConfig& config, uint32_t framesInFlight) {
    device_ = device;
    allocator_ = allocator;
    config_ = config;

    LOG_INFO("Exposure", "Creating exposure resources for %u frames", framesInFlight);

    buffers_.resize(framesInFlight);
    allocations_.resize(framesInFlight);
    allocationInfos_.resize(framesInFlight);

    // Buffer size includes histogram + exposure values
    VkDeviceSize bufferSize = sizeof(ExposureBufferData);

    // Create per-frame buffers
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkResult result = vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                                          &buffers_[i], &allocations_[i], &allocationInfos_[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create exposure buffer %u: %d", i, result);
            throw std::runtime_error("Failed to create exposure buffer");
        }

        // Initialize buffer with default values
        ExposureBufferData* data = static_cast<ExposureBufferData*>(allocationInfos_[i].pMappedData);
        std::memset(data->histogram, 0, sizeof(data->histogram));
        data->averageLuminance = config_.targetLuminance;
        data->currentExposure = INITIAL_EXPOSURE;
        data->targetExposure = INITIAL_EXPOSURE;
    }

    LOG_INFO("Exposure", "Exposure resources created successfully");
}

void ResourcesExposure::destroy() {
    if (allocator_ != VK_NULL_HANDLE) {
        for (size_t i = 0; i < buffers_.size(); ++i) {
            if (buffers_[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator_, buffers_[i], allocations_[i]);
            }
        }
        buffers_.clear();
        allocations_.clear();
        allocationInfos_.clear();
    }

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

void ResourcesExposure::resetHistogram(uint32_t frameIndex) {
    if (frameIndex >= allocationInfos_.size()) return;

    ExposureBufferData* data = static_cast<ExposureBufferData*>(
        allocationInfos_[frameIndex].pMappedData);
    if (data) {
        std::memset(data->histogram, 0, sizeof(data->histogram));
    }
}

VkDescriptorBufferInfo ResourcesExposure::getDescriptorInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo info{};
    if (frameIndex < buffers_.size()) {
        info.buffer = buffers_[frameIndex];
        info.offset = 0;
        info.range = sizeof(ExposureBufferData);
    }
    return info;
}

float ResourcesExposure::getCurrentExposure(uint32_t frameIndex) const {
    if (frameIndex >= allocationInfos_.size()) return INITIAL_EXPOSURE;

    const ExposureBufferData* data = static_cast<const ExposureBufferData*>(
        allocationInfos_[frameIndex].pMappedData);
    if (data) {
        return data->currentExposure;
    }
    return INITIAL_EXPOSURE;
}

} // namespace rendering
} // namespace jupiter









