#pragma once

/**
 * @file pipeline_frustum_culling.h
 * @brief GPU frustum culling compute pipeline
 *
 * Performs frustum culling on the GPU by modifying indirect draw commands.
 * Objects outside the view frustum have their instanceCount set to 0,
 * effectively skipping their draw calls.
 *
 * Performance:
 * - O(1) per object (parallel compute)
 * - No CPU readback required
 * - Integrates with indirect draw pipeline
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <string>

namespace jupiter::rendering {

// Forward declarations
class IndirectDrawBuffer;

namespace vulkan {
    class VulkanBuffer;
}

constexpr uint32_t CULLING_FRAMES_IN_FLIGHT = 2;

/**
 * @brief GPU frustum culling compute pipeline
 */
class PipelineFrustumCulling {
public:
    PipelineFrustumCulling() = default;
    ~PipelineFrustumCulling() { destroy(); }

    // Non-copyable
    PipelineFrustumCulling(const PipelineFrustumCulling&) = delete;
    PipelineFrustumCulling& operator=(const PipelineFrustumCulling&) = delete;

    bool initialize(VkDevice device, VmaAllocator allocator,
                   const std::string& shaderPath = "shaders/compute/frustum_culling.comp.spv");

    void destroy();

    void updateFrustum(const glm::mat4& viewProj, uint32_t frameIndex);

    void bindIndirectBuffer(IndirectDrawBuffer& indirectBuffer, uint32_t frameIndex);

    void execute(VkCommandBuffer cmd, IndirectDrawBuffer& indirectBuffer,
                uint32_t frameIndex);

    [[nodiscard]] bool isInitialized() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, CULLING_FRAMES_IN_FLIGHT> descriptorSets_;

    std::array<std::unique_ptr<vulkan::VulkanBuffer>, CULLING_FRAMES_IN_FLIGHT> frustumUBOs_;

    IndirectDrawBuffer* boundBuffer_ = nullptr;

    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createPipelineLayout();
    bool createPipeline(const std::string& shaderPath);
    bool createFrustumUBOs();
    bool loadShader(const std::string& path);

    void updateDescriptorSet(IndirectDrawBuffer& buffer, uint32_t frameIndex);
};

} // namespace jupiter::rendering
