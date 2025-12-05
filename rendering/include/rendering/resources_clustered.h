#pragma once

/**
 * @file resources_clustered.h
 * @brief GPU resources for clustered forward shading
 * 
 * Manages the data structures needed for clustered forward rendering:
 * - Cluster AABBs (view-space bounding boxes for each cluster)
 * - Light cells (offset and count for each cluster)
 * - Light indices (indices into the light buffer for each cluster)
 * - Global atomic counter for light index allocation
 */

#include "resources_base.h"
#include "render_features.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>

namespace jupiter::rendering {

// Constants
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

/**
 * @brief Axis-aligned bounding box for a cluster (view space)
 */
struct ClusterAABB {
    alignas(16) glm::vec4 minPoint;  // xyz = min corner, w = unused
    alignas(16) glm::vec4 maxPoint;  // xyz = max corner, w = unused
};

/**
 * @brief Light cell containing offset and count into light index buffer
 */
struct LightCell {
    alignas(4) uint32_t offset;  // Starting index in light indices buffer
    alignas(4) uint32_t count;   // Number of lights in this cluster
};

/**
 * @brief Point light data for clustered shading
 */
struct ClusteredLight {
    alignas(16) glm::vec4 position;  // xyz = world position, w = radius
    alignas(16) glm::vec4 color;     // xyz = color, w = intensity
};

/**
 * @brief GPU resources for clustered forward shading
 * 
 * Buffer layout:
 * - aabbBuffer: ClusterAABB[clusterCount] - Cluster bounding boxes
 * - lightCellsBuffer: LightCell[clusterCount] - Per-cluster light info
 * - lightIndicesBuffer: uint32_t[clusterCount * maxLightsPerCluster] - Light indices
 * - globalIndexCountBuffer: uint32_t - Atomic counter for allocation
 */
class ResourcesClusteredForward : public ResourcesBase {
public:
    ResourcesClusteredForward() = default;
    ~ResourcesClusteredForward() override { destroy(); }

    /**
     * @brief Create all GPU buffers
     * @param device Vulkan device
     * @param allocator VMA allocator (or nullptr for legacy)
     * @param config Cluster configuration
     */
    void create(VkDevice device, VkPhysicalDevice physicalDevice,
                const ClusteredForwardConfig& config);

    void destroy() override;

    void onWindowResized(uint32_t width, uint32_t height) override;

    const char* getName() const override { return "ResourcesClusteredForward"; }

    bool isValid() const override { return aabbBuffer_.valid(); }

    // ========================================================================
    // Accessors
    // ========================================================================

    const GPUBuffer& getAABBBuffer() const { return aabbBuffer_; }
    const GPUBuffer& getLightCellsBuffer() const { return lightCellsBuffer_; }
    const GPUBuffer& getLightIndicesBuffer() const { return lightIndicesBuffer_; }
    
    // Per-frame atomic counter (reset each frame)
    const GPUBuffer& getGlobalIndexCountBuffer(uint32_t frameIndex) const {
        return globalIndexCountBuffers_[frameIndex];
    }

    // Descriptor info for binding
    VkDescriptorBufferInfo getAABBDescriptor() const {
        return aabbBuffer_.getDescriptorInfo();
    }

    VkDescriptorBufferInfo getLightCellsDescriptor() const {
        return lightCellsBuffer_.getDescriptorInfo();
    }

    VkDescriptorBufferInfo getLightIndicesDescriptor() const {
        return lightIndicesBuffer_.getDescriptorInfo();
    }

    // ========================================================================
    // State
    // ========================================================================

    /**
     * @brief Mark AABBs as needing regeneration
     * 
     * Call when camera projection changes (FOV, near/far).
     */
    void markAABBDirty() { aabbDirty_ = true; }
    bool isAABBDirty() const { return aabbDirty_; }
    void clearAABBDirty() { aabbDirty_ = false; }

    /**
     * @brief Reset atomic counter for new frame
     */
    void resetGlobalIndexCount(uint32_t frameIndex);

    // ========================================================================
    // Configuration
    // ========================================================================

    const ClusteredForwardConfig& getConfig() const { return config_; }
    uint32_t getClusterCount() const { return config_.totalClusterCount(); }
    uint32_t getMaxLightIndices() const { 
        return config_.totalClusterCount() * config_.maxLightsPerCluster; 
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ClusteredForwardConfig config_;

    // Cluster AABB buffer (regenerated when projection changes)
    GPUBuffer aabbBuffer_;
    bool aabbDirty_ = true;

    // Light assignment buffers
    GPUBuffer lightCellsBuffer_;
    GPUBuffer lightIndicesBuffer_;

    // Per-frame atomic counter (reset by CPU each frame)
    std::array<GPUBuffer, MAX_FRAMES_IN_FLIGHT> globalIndexCountBuffers_;

    // Helper to create a storage buffer
    void createStorageBuffer(GPUBuffer& buffer, VkDeviceSize size, 
                            VkBufferUsageFlags additionalUsage = 0);
};

/**
 * @brief GPU resources for light data
 * 
 * Manages the SSBO containing all lights for clustered shading.
 */
class ResourcesLight : public ResourcesBase {
public:
    ResourcesLight() = default;
    ~ResourcesLight() override { destroy(); }

    /**
     * @brief Create light storage buffer
     * @param maxLights Maximum number of lights supported
     */
    void create(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxLights);

    void destroy() override;

    const char* getName() const override { return "ResourcesLight"; }

    bool isValid() const override { return storageBuffer_.valid(); }

    // ========================================================================
    // Light Management
    // ========================================================================

    /**
     * @brief Upload light data to GPU
     */
    void updateLights(const std::vector<ClusteredLight>& lights);

    /**
     * @brief Update a single light's position
     */
    void updateLightPosition(uint32_t index, const glm::vec3& position);

    /**
     * @brief Get current light count
     */
    uint32_t getLightCount() const { return lightCount_; }

    /**
     * @brief Get maximum light capacity
     */
    uint32_t getMaxLights() const { return maxLights_; }

    // ========================================================================
    // Accessors
    // ========================================================================

    const GPUBuffer& getStorageBuffer() const { return storageBuffer_; }

    VkDescriptorBufferInfo getDescriptor() const {
        return storageBuffer_.getDescriptorInfo();
    }

    // Direct access for CPU-side updates
    std::vector<ClusteredLight>& getLightsRef() { return lights_; }
    const std::vector<ClusteredLight>& getLights() const { return lights_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    
    GPUBuffer storageBuffer_;
    std::vector<ClusteredLight> lights_;
    uint32_t lightCount_ = 0;
    uint32_t maxLights_ = 0;
};

} // namespace jupiter::rendering

