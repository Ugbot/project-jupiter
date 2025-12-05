#pragma once

/**
 * @file indirect_draw_buffer.h
 * @brief GPU-driven indirect draw command management
 *
 * Enables GPU-driven rendering where the GPU decides what to draw
 * via compute shaders that modify indirect draw commands.
 *
 * Key Features:
 * - Pre-allocated command buffers (no runtime allocation)
 * - GPU-writable for compute culling
 * - CPU-readable for debugging
 * - Integrates with ECS instance data
 *
 * Usage:
 * @code
 *   IndirectDrawBuffer buffer;
 *   buffer.initialize(device, allocator, maxInstances);
 *
 *   // Fill from ECS or mesh data
 *   buffer.beginBatch();
 *   buffer.addDrawCommand(meshData);
 *   buffer.endBatch(cmd);
 *
 *   // GPU culling modifies instanceCount to 0 for culled objects
 *   frustumCullingPipeline.execute(cmd, buffer.getBuffer());
 *
 *   // Draw with indirect
 *   vkCmdDrawIndirect(cmd, buffer.getBuffer(), 0, drawCount, stride);
 * @endcode
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace jupiter::rendering {

// Forward declarations
namespace vulkan {
    class VulkanBuffer;
}

/**
 * @brief GPU AABB structure (matches shader layout)
 */
struct alignas(16) GPUAABB {
    glm::vec4 minPoint;  // xyz = min corner, w = padding
    glm::vec4 maxPoint;  // xyz = max corner, w = padding
};

/**
 * @brief Frustum planes and corners for GPU culling
 */
struct alignas(16) GPUFrustum {
    glm::vec4 planes[6];   // 6 frustum planes (ax + by + cz + d = 0)
    glm::vec4 corners[8];  // 8 frustum corners for reverse test
};

/**
 * @brief Maximum instances for pre-allocation
 */
constexpr uint32_t MAX_INDIRECT_DRAWS = 65536;

/**
 * @brief Manages GPU indirect draw commands
 *
 * The IndirectDrawBuffer holds VkDrawIndirectCommand structures that
 * can be modified by compute shaders for GPU-driven culling.
 */
class IndirectDrawBuffer {
public:
    IndirectDrawBuffer() = default;
    ~IndirectDrawBuffer() { destroy(); }

    // Non-copyable
    IndirectDrawBuffer(const IndirectDrawBuffer&) = delete;
    IndirectDrawBuffer& operator=(const IndirectDrawBuffer&) = delete;

    /**
     * @brief Initialize the indirect draw buffer
     *
     * Pre-allocates GPU buffers for maxDraws indirect commands.
     * Follows CLAUDE.md: No runtime allocations.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param maxDraws Maximum number of draw commands
     * @return true if successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator,
                   uint32_t maxDraws = MAX_INDIRECT_DRAWS);

    /**
     * @brief Destroy all GPU resources
     */
    void destroy();

    /**
     * @brief Begin building a new batch of draw commands
     */
    void beginBatch();

    /**
     * @brief Add a draw command to the batch
     *
     * @param vertexCount Number of vertices to draw
     * @param firstVertex Offset into vertex buffer
     * @param instanceIndex Instance index (for firstInstance)
     */
    void addDrawCommand(uint32_t vertexCount, uint32_t firstVertex,
                       uint32_t instanceIndex);

    /**
     * @brief Add a draw command with AABB for culling
     *
     * @param vertexCount Number of vertices to draw
     * @param firstVertex Offset into vertex buffer
     * @param instanceIndex Instance index
     * @param aabb World-space AABB for frustum culling
     */
    void addDrawCommandWithAABB(uint32_t vertexCount, uint32_t firstVertex,
                                uint32_t instanceIndex, const GPUAABB& aabb);

    /**
     * @brief End batch and upload to GPU
     *
     * Copies staged commands to device-local buffer.
     *
     * @param cmd Command buffer for transfer operations
     */
    void endBatch(VkCommandBuffer cmd);

    /**
     * @brief Reset all instance counts to 1 (undo culling)
     *
     * Call before frustum culling to reset visibility.
     *
     * @param cmd Command buffer for compute operations
     */
    void resetCulling(VkCommandBuffer cmd);

    /**
     * @brief Get the indirect command buffer for binding
     */
    [[nodiscard]] VkBuffer getCommandBuffer() const;

    /**
     * @brief Get the AABB buffer for culling shader
     */
    [[nodiscard]] VkBuffer getAABBBuffer() const;

    /**
     * @brief Get current draw count
     */
    [[nodiscard]] uint32_t getDrawCount() const { return drawCount_; }

    /**
     * @brief Get descriptor buffer info for command buffer
     */
    [[nodiscard]] VkDescriptorBufferInfo getCommandBufferInfo() const;

    /**
     * @brief Get descriptor buffer info for AABB buffer
     */
    [[nodiscard]] VkDescriptorBufferInfo getAABBBufferInfo() const;

    /**
     * @brief Check if initialized
     */
    [[nodiscard]] bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

    /**
     * @brief Get stride between draw commands
     */
    static constexpr VkDeviceSize getCommandStride() {
        return sizeof(VkDrawIndirectCommand);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // GPU buffers
    std::unique_ptr<vulkan::VulkanBuffer> commandBuffer_;      // Device-local commands
    std::unique_ptr<vulkan::VulkanBuffer> commandStaging_;     // Host-visible staging
    std::unique_ptr<vulkan::VulkanBuffer> aabbBuffer_;         // Device-local AABBs
    std::unique_ptr<vulkan::VulkanBuffer> aabbStaging_;        // Host-visible staging

    // CPU-side staging vectors (pre-allocated)
    std::vector<VkDrawIndirectCommand> stagedCommands_;
    std::vector<GPUAABB> stagedAABBs_;

    uint32_t maxDraws_ = 0;
    uint32_t drawCount_ = 0;
};

/**
 * @brief Utility to extract frustum from view-projection matrix
 *
 * @param viewProj Combined view-projection matrix
 * @return Frustum structure for GPU culling
 */
GPUFrustum extractFrustum(const glm::mat4& viewProj);

/**
 * @brief Compute AABB corners
 *
 * @param aabb Input AABB
 * @param corners Output array of 8 corner positions
 */
void computeAABBCorners(const GPUAABB& aabb, glm::vec3 corners[8]);

/**
 * @brief Transform AABB by matrix
 *
 * @param aabb Local-space AABB
 * @param transform World transform matrix
 * @return World-space AABB
 */
GPUAABB transformAABB(const GPUAABB& aabb, const glm::mat4& transform);

} // namespace jupiter::rendering
