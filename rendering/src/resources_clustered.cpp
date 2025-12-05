/**
 * @file resources_clustered.cpp
 * @brief Implementation of clustered forward shading resources
 */

#include "rendering/resources_clustered.h"
#include <cstring>
#include <stdexcept>

namespace jupiter::rendering {

// ============================================================================
// Helper: Find memory type
// ============================================================================

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                               uint32_t typeFilter,
                               VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

// ============================================================================
// ResourcesClusteredForward
// ============================================================================

void ResourcesClusteredForward::create(VkDevice device, VkPhysicalDevice physicalDevice,
                                       const ClusteredForwardConfig& config) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    config_ = config;

    const uint32_t clusterCount = config_.totalClusterCount();
    const uint32_t maxLightIndices = getMaxLightIndices();

    // Create AABB buffer (storage, updated by compute shader)
    createStorageBuffer(aabbBuffer_, clusterCount * sizeof(ClusterAABB));

    // Create light cells buffer
    createStorageBuffer(lightCellsBuffer_, clusterCount * sizeof(LightCell));

    // Create light indices buffer
    createStorageBuffer(lightIndicesBuffer_, maxLightIndices * sizeof(uint32_t));

    // Create per-frame atomic counter buffers
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createStorageBuffer(globalIndexCountBuffers_[i], sizeof(uint32_t),
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    aabbDirty_ = true;
}

void ResourcesClusteredForward::destroy() {
    auto destroyBuffer = [this](GPUBuffer& buf) {
        if (buf.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buf.buffer, nullptr);
            buf.buffer = VK_NULL_HANDLE;
        }
        if (buf.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, buf.memory, nullptr);
            buf.memory = VK_NULL_HANDLE;
        }
        buf.size = 0;
    };

    destroyBuffer(aabbBuffer_);
    destroyBuffer(lightCellsBuffer_);
    destroyBuffer(lightIndicesBuffer_);

    for (auto& buf : globalIndexCountBuffers_) {
        destroyBuffer(buf);
    }

    device_ = VK_NULL_HANDLE;
}

void ResourcesClusteredForward::onWindowResized(uint32_t width, uint32_t height) {
    // Cluster AABBs depend on screen dimensions, mark for regeneration
    aabbDirty_ = true;
}

void ResourcesClusteredForward::resetGlobalIndexCount(uint32_t frameIndex) {
    // Reset atomic counter to 0
    // This needs to be done via a command buffer transfer, or host-visible memory
    GPUBuffer& buf = globalIndexCountBuffers_[frameIndex];
    if (buf.mappedData) {
        uint32_t zero = 0;
        std::memcpy(buf.mappedData, &zero, sizeof(uint32_t));
    }
}

void ResourcesClusteredForward::createStorageBuffer(GPUBuffer& buffer, VkDeviceSize size,
                                                    VkBufferUsageFlags additionalUsage) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | additionalUsage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer.buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create storage buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, buffer.buffer, &memRequirements);

    // Use device-local memory for best performance
    // For the atomic counter, we need host-visible to reset it
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (additionalUsage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
        // Atomic counter - use host visible for easy reset
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice_, memRequirements.memoryTypeBits, memProps);

    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device_, buffer.buffer, nullptr);
        throw std::runtime_error("Failed to find suitable memory type for storage buffer");
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &buffer.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer.buffer, nullptr);
        throw std::runtime_error("Failed to allocate storage buffer memory");
    }

    vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0);
    buffer.size = size;

    // Map memory for host-visible buffers
    if (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(device_, buffer.memory, 0, size, 0, &buffer.mappedData);
    }
}

// ============================================================================
// ResourcesLight
// ============================================================================

void ResourcesLight::create(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxLights) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    maxLights_ = maxLights;
    lightCount_ = 0;
    lights_.reserve(maxLights);

    VkDeviceSize bufferSize = maxLights * sizeof(ClusteredLight);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &storageBuffer_.buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light storage buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, storageBuffer_.buffer, &memRequirements);

    // Use host-visible memory for easy updates
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice_, memRequirements.memoryTypeBits, memProps);

    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device_, storageBuffer_.buffer, nullptr);
        throw std::runtime_error("Failed to find suitable memory type for light buffer");
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &storageBuffer_.memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, storageBuffer_.buffer, nullptr);
        throw std::runtime_error("Failed to allocate light buffer memory");
    }

    vkBindBufferMemory(device_, storageBuffer_.buffer, storageBuffer_.memory, 0);
    storageBuffer_.size = bufferSize;

    // Map for persistent access
    vkMapMemory(device_, storageBuffer_.memory, 0, bufferSize, 0, &storageBuffer_.mappedData);
}

void ResourcesLight::destroy() {
    if (storageBuffer_.mappedData) {
        vkUnmapMemory(device_, storageBuffer_.memory);
        storageBuffer_.mappedData = nullptr;
    }

    if (storageBuffer_.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, storageBuffer_.buffer, nullptr);
        storageBuffer_.buffer = VK_NULL_HANDLE;
    }

    if (storageBuffer_.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, storageBuffer_.memory, nullptr);
        storageBuffer_.memory = VK_NULL_HANDLE;
    }

    storageBuffer_.size = 0;
    lights_.clear();
    lightCount_ = 0;
    device_ = VK_NULL_HANDLE;
}

void ResourcesLight::updateLights(const std::vector<ClusteredLight>& lights) {
    if (lights.size() > maxLights_) {
        throw std::runtime_error("Light count exceeds maximum capacity");
    }

    lights_ = lights;
    lightCount_ = static_cast<uint32_t>(lights.size());

    if (storageBuffer_.mappedData && lightCount_ > 0) {
        std::memcpy(storageBuffer_.mappedData, lights_.data(), 
                   lightCount_ * sizeof(ClusteredLight));
    }
}

void ResourcesLight::updateLightPosition(uint32_t index, const glm::vec3& position) {
    if (index >= lightCount_) {
        return;
    }

    lights_[index].position = glm::vec4(position, lights_[index].position.w);

    // Update GPU buffer
    if (storageBuffer_.mappedData) {
        ClusteredLight* gpuLights = static_cast<ClusteredLight*>(storageBuffer_.mappedData);
        gpuLights[index].position = lights_[index].position;
    }
}

} // namespace jupiter::rendering

