#pragma once

/**
 * @file pipeline_clustered.h
 * @brief Compute pipelines for clustered forward shading
 * 
 * Implements:
 * - PipelineAABBGenerator: Compute shader to generate cluster AABBs
 * - PipelineLightCulling: Compute shader to assign lights to clusters
 */

#include "pipeline_base.h"
#include "resources_clustered.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace jupiter::rendering {

/**
 * @brief Push constants for AABB generation
 */
struct AABBGeneratorPushConstants {
    alignas(16) glm::mat4 inverseProjection;
    alignas(4) uint32_t clusterCountX;
    alignas(4) uint32_t clusterCountY;
    alignas(4) uint32_t clusterCountZ;
    alignas(4) float zNear;
    alignas(4) float zFar;
    alignas(4) uint32_t screenWidth;
    alignas(4) uint32_t screenHeight;
    alignas(4) float padding;
};

/**
 * @brief Push constants for light culling
 */
struct LightCullingPushConstants {
    alignas(16) glm::mat4 viewMatrix;
    alignas(4) uint32_t clusterCountX;
    alignas(4) uint32_t clusterCountY;
    alignas(4) uint32_t clusterCountZ;
    alignas(4) uint32_t lightCount;
    alignas(4) uint32_t maxLightsPerCluster;
    alignas(4) float padding[3];
};

/**
 * @brief Compute pipeline to generate cluster AABBs
 * 
 * Generates view-space AABBs for each cluster based on:
 * - Screen-space tile positions
 * - Depth slice distribution
 * - Camera projection matrix
 * 
 * Only needs to run when projection changes (resize, FOV change).
 */
class PipelineAABBGenerator {
public:
    PipelineAABBGenerator() = default;
    ~PipelineAABBGenerator();

    /**
     * @brief Create the compute pipeline
     */
    void create(VkDevice device, const ClusteredForwardConfig& config);

    /**
     * @brief Destroy all resources
     */
    void destroy();

    /**
     * @brief Record compute commands
     * @param cmd Command buffer to record into
     * @param resources Clustered forward resources
     * @param inverseProjection Inverse projection matrix
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     */
    void dispatch(VkCommandBuffer cmd,
                  ResourcesClusteredForward& resources,
                  const glm::mat4& inverseProjection,
                  uint32_t screenWidth,
                  uint32_t screenHeight);

    bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    ClusteredForwardConfig config_;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    void createDescriptorSetLayout();
    void createPipelineLayout();
    void createComputePipeline();
    VkShaderModule loadShaderModule(const std::string& filename);
};

/**
 * @brief Compute pipeline to cull lights into clusters
 * 
 * For each cluster, tests all lights against the cluster AABB
 * and builds the light index list. Uses atomic operations for
 * thread-safe index allocation.
 * 
 * Runs every frame before the forward pass.
 */
class PipelineLightCulling {
public:
    PipelineLightCulling() = default;
    ~PipelineLightCulling();

    /**
     * @brief Create the compute pipeline
     */
    void create(VkDevice device, const ClusteredForwardConfig& config);

    /**
     * @brief Destroy all resources
     */
    void destroy();

    /**
     * @brief Record compute commands
     * @param cmd Command buffer
     * @param clusteredResources Cluster data
     * @param lightResources Light data
     * @param viewMatrix Current view matrix
     * @param frameIndex Current frame index (for atomic counter)
     */
    void dispatch(VkCommandBuffer cmd,
                  ResourcesClusteredForward& clusteredResources,
                  ResourcesLight& lightResources,
                  const glm::mat4& viewMatrix,
                  uint32_t frameIndex);

    /**
     * @brief Update descriptor set with new resources
     */
    void updateDescriptors(ResourcesClusteredForward& clusteredResources,
                          ResourcesLight& lightResources,
                          uint32_t frameIndex);

    bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    ClusteredForwardConfig config_;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets_{};

    void createDescriptorSetLayout();
    void createPipelineLayout();
    void createComputePipeline();
    VkShaderModule loadShaderModule(const std::string& filename);
};

/**
 * @brief Combined clustered forward pipeline manager
 * 
 * Convenience class that owns both compute pipelines and
 * handles the full clustered shading setup.
 */
class ClusteredForwardPipelines {
public:
    ClusteredForwardPipelines() = default;
    ~ClusteredForwardPipelines() { destroy(); }

    /**
     * @brief Create all clustered forward pipelines
     */
    void create(VkDevice device, VkPhysicalDevice physicalDevice,
                const ClusteredForwardConfig& config,
                uint32_t maxLights);

    /**
     * @brief Destroy all pipelines and resources
     */
    void destroy();

    /**
     * @brief Run the clustered shading compute passes
     * @param cmd Command buffer
     * @param viewMatrix Current view matrix
     * @param inverseProjection Inverse projection matrix
     * @param screenWidth Current screen width
     * @param screenHeight Current screen height
     * @param frameIndex Current frame in flight
     */
    void execute(VkCommandBuffer cmd,
                 const glm::mat4& viewMatrix,
                 const glm::mat4& inverseProjection,
                 uint32_t screenWidth,
                 uint32_t screenHeight,
                 uint32_t frameIndex);

    // Resource access for forward pass binding
    ResourcesClusteredForward& getClusteredResources() { return clusteredResources_; }
    ResourcesLight& getLightResources() { return lightResources_; }

    // Light management
    void setLights(const std::vector<ClusteredLight>& lights) {
        lightResources_.updateLights(lights);
    }

    void updateLightPosition(uint32_t index, const glm::vec3& position) {
        lightResources_.updateLightPosition(index, position);
    }

    bool isValid() const {
        return aabbGenerator_.isValid() && lightCulling_.isValid();
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    ClusteredForwardConfig config_;

    PipelineAABBGenerator aabbGenerator_;
    PipelineLightCulling lightCulling_;
    ResourcesClusteredForward clusteredResources_;
    ResourcesLight lightResources_;
};

} // namespace jupiter::rendering

