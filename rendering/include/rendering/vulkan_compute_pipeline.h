#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace jupiter {
namespace rendering {
namespace vulkan {

/**
 * @brief Vulkan compute pipeline for GPU compute workloads
 *
 * Manages compute pipeline creation, descriptor sets, and dispatch operations.
 * Used for IBL generation (BRDF LUT), particle systems, physics, etc.
 *
 * Usage:
 * @code
 * VulkanComputePipeline pipeline;
 * pipeline.create(device, "shader.comp.spv", descriptorSetLayout, &pushConstant);
 *
 * vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getPipeline());
 * vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);
 * pipeline.dispatch(cmd, workGroupsX, workGroupsY, workGroupsZ);
 * @endcode
 */
class VulkanComputePipeline {
public:
    VulkanComputePipeline() = default;
    ~VulkanComputePipeline() { destroy(); }

    // Non-copyable, movable
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&&) noexcept = default;
    VulkanComputePipeline& operator=(VulkanComputePipeline&&) noexcept = default;

    /**
     * @brief Create compute pipeline from shader file
     *
     * @param device Vulkan device
     * @param shaderPath Path to compiled SPIR-V compute shader (.comp.spv)
     * @param descriptorSetLayouts Descriptor set layouts (can be empty)
     * @param pushConstantRange Optional push constant range (nullptr if none)
     * @return true if successful
     */
    bool create(VkDevice device,
                const std::string& shaderPath,
                const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
                const VkPushConstantRange* pushConstantRange = nullptr);

    /**
     * @brief Destroy pipeline and resources
     */
    void destroy();

    /**
     * @brief Dispatch compute work
     *
     * Call after binding pipeline and descriptor sets.
     *
     * @param commandBuffer Command buffer
     * @param groupCountX Number of work groups in X dimension
     * @param groupCountY Number of work groups in Y dimension
     * @param groupCountZ Number of work groups in Z dimension
     */
    void dispatch(VkCommandBuffer commandBuffer,
                  uint32_t groupCountX,
                  uint32_t groupCountY,
                  uint32_t groupCountZ) const;

    /**
     * @brief Insert pipeline barrier for compute-to-compute dependency
     *
     * @param commandBuffer Command buffer
     * @param srcStage Source pipeline stage
     * @param dstStage Destination pipeline stage
     * @param srcAccess Source access mask
     * @param dstAccess Destination access mask
     */
    static void barrier(VkCommandBuffer commandBuffer,
                       VkPipelineStageFlags srcStage,
                       VkPipelineStageFlags dstStage,
                       VkAccessFlags srcAccess,
                       VkAccessFlags dstAccess);

    /**
     * @brief Insert memory barrier for image after compute write
     *
     * @param commandBuffer Command buffer
     * @param image Image to transition
     * @param oldLayout Old image layout
     * @param newLayout New image layout
     * @param srcStage Source pipeline stage
     * @param dstStage Destination pipeline stage
     * @param srcAccess Source access mask
     * @param dstAccess Destination access mask
     */
    static void imageBarrier(VkCommandBuffer commandBuffer,
                            VkImage image,
                            VkImageLayout oldLayout,
                            VkImageLayout newLayout,
                            VkPipelineStageFlags srcStage,
                            VkPipelineStageFlags dstStage,
                            VkAccessFlags srcAccess,
                            VkAccessFlags dstAccess);

    // Getters
    VkPipeline getPipeline() const { return pipeline_; }
    VkPipelineLayout getLayout() const { return pipelineLayout_; }
    bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;

    /**
     * @brief Load SPIR-V shader from file
     */
    bool loadShader(const std::string& filepath);

    /**
     * @brief Create shader module from SPIR-V code
     */
    bool createShaderModule(const std::vector<uint32_t>& code);
};

} // namespace vulkan
} // namespace rendering
} // namespace jupiter
