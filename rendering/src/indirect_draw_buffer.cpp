/**
 * @file indirect_draw_buffer.cpp
 * @brief Implementation of GPU-driven indirect draw buffer
 */

#include "rendering/indirect_draw_buffer.h"
#include "vulkan_backend.h"
#include "logging/logging.h"

#include <cstring>
#include <algorithm>

namespace jupiter::rendering {

bool IndirectDrawBuffer::initialize(VkDevice device, VmaAllocator allocator,
                                    uint32_t maxDraws) {
    device_ = device;
    allocator_ = allocator;
    maxDraws_ = maxDraws;

    // Pre-allocate CPU staging vectors
    stagedCommands_.reserve(maxDraws);
    stagedAABBs_.reserve(maxDraws);

    // Calculate buffer sizes
    VkDeviceSize commandBufferSize = maxDraws * sizeof(VkDrawIndirectCommand);
    VkDeviceSize aabbBufferSize = maxDraws * sizeof(GPUAABB);

    // Create command buffers
    commandBuffer_ = std::make_unique<vulkan::VulkanBuffer>();
    commandStaging_ = std::make_unique<vulkan::VulkanBuffer>();

    // Device-local command buffer (used by GPU for indirect draw + culling writes)
    if (!commandBuffer_->create(allocator, commandBufferSize,
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  // For compute shader writes
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY)) {
        LOG_ERROR("IndirectDraw", "Failed to create command buffer");
        destroy();
        return false;
    }

    // Host-visible staging for command upload
    if (!commandStaging_->create(allocator, commandBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        LOG_ERROR("IndirectDraw", "Failed to create command staging buffer");
        destroy();
        return false;
    }

    // Create AABB buffers
    aabbBuffer_ = std::make_unique<vulkan::VulkanBuffer>();
    aabbStaging_ = std::make_unique<vulkan::VulkanBuffer>();

    // Device-local AABB buffer (read by culling compute shader)
    if (!aabbBuffer_->create(allocator, aabbBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY)) {
        LOG_ERROR("IndirectDraw", "Failed to create AABB buffer");
        destroy();
        return false;
    }

    // Host-visible staging for AABB upload
    if (!aabbStaging_->create(allocator, aabbBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        LOG_ERROR("IndirectDraw", "Failed to create AABB staging buffer");
        destroy();
        return false;
    }

    LOG_INFO("IndirectDraw", "Initialized with %u max draws (%.2f MB total)",
             maxDraws,
             (commandBufferSize + aabbBufferSize) / (1024.0f * 1024.0f));

    return true;
}

void IndirectDrawBuffer::destroy() {
    if (commandBuffer_) {
        commandBuffer_->destroy();
        commandBuffer_.reset();
    }
    if (commandStaging_) {
        commandStaging_->destroy();
        commandStaging_.reset();
    }
    if (aabbBuffer_) {
        aabbBuffer_->destroy();
        aabbBuffer_.reset();
    }
    if (aabbStaging_) {
        aabbStaging_->destroy();
        aabbStaging_.reset();
    }

    stagedCommands_.clear();
    stagedAABBs_.clear();
    drawCount_ = 0;

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

void IndirectDrawBuffer::beginBatch() {
    // Clear without deallocating (keep capacity)
    stagedCommands_.clear();
    stagedAABBs_.clear();
    drawCount_ = 0;
}

void IndirectDrawBuffer::addDrawCommand(uint32_t vertexCount, uint32_t firstVertex,
                                        uint32_t instanceIndex) {
    if (drawCount_ >= maxDraws_) {
        LOG_ERROR("IndirectDraw", "Max draws exceeded (%u)", maxDraws_);
        return;
    }

    VkDrawIndirectCommand cmd = {};
    cmd.vertexCount = vertexCount;
    cmd.instanceCount = 1;  // Will be set to 0 by culling if invisible
    cmd.firstVertex = firstVertex;
    cmd.firstInstance = instanceIndex;

    stagedCommands_.push_back(cmd);

    // Add placeholder AABB (infinite bounds - never culled)
    GPUAABB infiniteAABB = {};
    infiniteAABB.minPoint = glm::vec4(-1e10f, -1e10f, -1e10f, 0.0f);
    infiniteAABB.maxPoint = glm::vec4(1e10f, 1e10f, 1e10f, 0.0f);
    stagedAABBs_.push_back(infiniteAABB);

    ++drawCount_;
}

void IndirectDrawBuffer::addDrawCommandWithAABB(uint32_t vertexCount, uint32_t firstVertex,
                                                uint32_t instanceIndex, const GPUAABB& aabb) {
    if (drawCount_ >= maxDraws_) {
        LOG_ERROR("IndirectDraw", "Max draws exceeded (%u)", maxDraws_);
        return;
    }

    VkDrawIndirectCommand cmd = {};
    cmd.vertexCount = vertexCount;
    cmd.instanceCount = 1;
    cmd.firstVertex = firstVertex;
    cmd.firstInstance = instanceIndex;

    stagedCommands_.push_back(cmd);
    stagedAABBs_.push_back(aabb);

    ++drawCount_;
}

void IndirectDrawBuffer::endBatch(VkCommandBuffer cmd) {
    if (drawCount_ == 0) {
        return;
    }

    // Upload commands to staging
    VkDeviceSize commandSize = drawCount_ * sizeof(VkDrawIndirectCommand);
    if (!commandStaging_->upload(stagedCommands_.data(), commandSize)) {
        LOG_ERROR("IndirectDraw", "Failed to upload commands to staging");
        return;
    }

    // Upload AABBs to staging
    VkDeviceSize aabbSize = drawCount_ * sizeof(GPUAABB);
    if (!aabbStaging_->upload(stagedAABBs_.data(), aabbSize)) {
        LOG_ERROR("IndirectDraw", "Failed to upload AABBs to staging");
        return;
    }

    // Copy from staging to device-local
    VkBufferCopy commandCopy = {};
    commandCopy.size = commandSize;
    vkCmdCopyBuffer(cmd, commandStaging_->getBuffer(),
                    commandBuffer_->getBuffer(), 1, &commandCopy);

    VkBufferCopy aabbCopy = {};
    aabbCopy.size = aabbSize;
    vkCmdCopyBuffer(cmd, aabbStaging_->getBuffer(),
                    aabbBuffer_->getBuffer(), 1, &aabbCopy);

    // Barrier to ensure copies complete before compute/draw
    VkBufferMemoryBarrier barriers[2] = {};

    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                                VK_ACCESS_SHADER_READ_BIT |
                                VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].buffer = commandBuffer_->getBuffer();
    barriers[0].offset = 0;
    barriers[0].size = commandSize;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].buffer = aabbBuffer_->getBuffer();
    barriers[1].offset = 0;
    barriers[1].size = aabbSize;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        2, barriers,
        0, nullptr);
}

void IndirectDrawBuffer::resetCulling(VkCommandBuffer cmd) {
    // This would reset all instanceCount to 1
    // For now, we rebuild the batch each frame instead
    // A more efficient approach would use a compute shader to reset
}

VkBuffer IndirectDrawBuffer::getCommandBuffer() const {
    return commandBuffer_ ? commandBuffer_->getBuffer() : VK_NULL_HANDLE;
}

VkBuffer IndirectDrawBuffer::getAABBBuffer() const {
    return aabbBuffer_ ? aabbBuffer_->getBuffer() : VK_NULL_HANDLE;
}

VkDescriptorBufferInfo IndirectDrawBuffer::getCommandBufferInfo() const {
    VkDescriptorBufferInfo info = {};
    if (commandBuffer_) {
        info.buffer = commandBuffer_->getBuffer();
        info.offset = 0;
        info.range = drawCount_ * sizeof(VkDrawIndirectCommand);
    }
    return info;
}

VkDescriptorBufferInfo IndirectDrawBuffer::getAABBBufferInfo() const {
    VkDescriptorBufferInfo info = {};
    if (aabbBuffer_) {
        info.buffer = aabbBuffer_->getBuffer();
        info.offset = 0;
        info.range = drawCount_ * sizeof(GPUAABB);
    }
    return info;
}

// ============================================================================
// Frustum Utilities
// ============================================================================

GPUFrustum extractFrustum(const glm::mat4& viewProj) {
    GPUFrustum frustum = {};

    // Extract planes from view-projection matrix (Gribb/Hartmann method)
    // Left plane
    frustum.planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]);

    // Right plane
    frustum.planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]);

    // Bottom plane
    frustum.planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]);

    // Top plane
    frustum.planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]);

    // Near plane
    frustum.planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]);

    // Far plane
    frustum.planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(frustum.planes[i]));
        if (len > 0.0f) {
            frustum.planes[i] /= len;
        }
    }

    // Compute frustum corners from inverse viewProj
    glm::mat4 invViewProj = glm::inverse(viewProj);
    const glm::vec4 ndcCorners[8] = {
        glm::vec4(-1, -1, 0, 1), glm::vec4( 1, -1, 0, 1),
        glm::vec4(-1,  1, 0, 1), glm::vec4( 1,  1, 0, 1),
        glm::vec4(-1, -1, 1, 1), glm::vec4( 1, -1, 1, 1),
        glm::vec4(-1,  1, 1, 1), glm::vec4( 1,  1, 1, 1),
    };

    for (int i = 0; i < 8; ++i) {
        glm::vec4 world = invViewProj * ndcCorners[i];
        frustum.corners[i] = world / world.w;
    }

    return frustum;
}

void computeAABBCorners(const GPUAABB& aabb, glm::vec3 corners[8]) {
    corners[0] = glm::vec3(aabb.minPoint.x, aabb.minPoint.y, aabb.minPoint.z);
    corners[1] = glm::vec3(aabb.maxPoint.x, aabb.minPoint.y, aabb.minPoint.z);
    corners[2] = glm::vec3(aabb.minPoint.x, aabb.maxPoint.y, aabb.minPoint.z);
    corners[3] = glm::vec3(aabb.maxPoint.x, aabb.maxPoint.y, aabb.minPoint.z);
    corners[4] = glm::vec3(aabb.minPoint.x, aabb.minPoint.y, aabb.maxPoint.z);
    corners[5] = glm::vec3(aabb.maxPoint.x, aabb.minPoint.y, aabb.maxPoint.z);
    corners[6] = glm::vec3(aabb.minPoint.x, aabb.maxPoint.y, aabb.maxPoint.z);
    corners[7] = glm::vec3(aabb.maxPoint.x, aabb.maxPoint.y, aabb.maxPoint.z);
}

GPUAABB transformAABB(const GPUAABB& aabb, const glm::mat4& transform) {
    // Transform all 8 corners and compute new AABB
    glm::vec3 corners[8];
    computeAABBCorners(aabb, corners);

    glm::vec3 newMin(1e10f);
    glm::vec3 newMax(-1e10f);

    for (int i = 0; i < 8; ++i) {
        glm::vec4 transformed = transform * glm::vec4(corners[i], 1.0f);
        glm::vec3 point = glm::vec3(transformed) / transformed.w;
        newMin = glm::min(newMin, point);
        newMax = glm::max(newMax, point);
    }

    GPUAABB result;
    result.minPoint = glm::vec4(newMin, 0.0f);
    result.maxPoint = glm::vec4(newMax, 0.0f);
    return result;
}

} // namespace jupiter::rendering
