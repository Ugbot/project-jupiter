/**
 * @file ecs_bridge.cpp
 * @brief Implementation of decoupled ECS-to-Renderer bridge
 *
 * The ECS bridge is designed to be completely optional - games can run
 * headless (no GPU) by simply not initializing the ECSRenderer.
 */

#include "rendering/ecs_bridge.h"
#include "vulkan_backend.h"
#include "ecs/world.h"
#include "logging/logging.h"

#include <algorithm>
#include <cstring>

namespace jupiter::rendering {

// ============================================================================
// PerFrame Special Members (unique_ptr requires complete type in .cpp)
// ============================================================================

FrameRing::PerFrame::~PerFrame() = default;

FrameRing::PerFrame::PerFrame(PerFrame&& other) noexcept
    : instanceBuffer(std::move(other.instanceBuffer))
    , stagingBuffer(std::move(other.stagingBuffer))
    , uploadFence(other.uploadFence)
    , lastGeneration(other.lastGeneration)
    , mappedStaging(other.mappedStaging)
    , initialized(other.initialized) {
    other.uploadFence = VK_NULL_HANDLE;
    other.mappedStaging = nullptr;
    other.initialized = false;
}

FrameRing::PerFrame& FrameRing::PerFrame::operator=(PerFrame&& other) noexcept {
    if (this != &other) {
        instanceBuffer = std::move(other.instanceBuffer);
        stagingBuffer = std::move(other.stagingBuffer);
        uploadFence = other.uploadFence;
        lastGeneration = other.lastGeneration;
        mappedStaging = other.mappedStaging;
        initialized = other.initialized;

        other.uploadFence = VK_NULL_HANDLE;
        other.mappedStaging = nullptr;
        other.initialized = false;
    }
    return *this;
}

// ============================================================================
// RenderFrame Implementation
// ============================================================================

void RenderFrame::uploadAsync(VkCommandBuffer cmd) {
    if (!needsUpload() || !stagingBuffer || !gpuBuffer) {
        return;
    }

    // Copy from staging to device-local
    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = instanceCount * sizeof(ecs::GPUInstanceData);

    vkCmdCopyBuffer(cmd,
                    stagingBuffer->getBuffer(),
                    gpuBuffer->getBuffer(),
                    1, &copyRegion);

    // Memory barrier to ensure copy completes before vertex shader reads
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = gpuBuffer->getBuffer();
    barrier.offset = 0;
    barrier.size = copyRegion.size;

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                        0,
                        0, nullptr,
                        1, &barrier,
                        0, nullptr);

    uploadPending = true;
    onDevice = true;
}

VkDescriptorBufferInfo RenderFrame::getBufferInfo() const noexcept {
    VkDescriptorBufferInfo info = {};
    if (gpuBuffer) {
        info.buffer = gpuBuffer->getBuffer();
        info.offset = 0;
        info.range = instanceCount * sizeof(ecs::GPUInstanceData);
    }
    return info;
}

// ============================================================================
// RenderFrameBuilder Implementation
// ============================================================================

RenderFrameBuilder::RenderFrameBuilder() {
    // Pre-allocate for max instances (no runtime allocation)
    instances_.reserve(MAX_RENDER_INSTANCES);
}

void RenderFrameBuilder::beginFrame(uint64_t generation) {
    clear();
    generation_ = generation;
}

void RenderFrameBuilder::clear() noexcept {
    instanceCount_ = 0;
    // Don't shrink vector - keep capacity for reuse
}

size_t RenderFrameBuilder::extractFromSnapshot(const ecs::ReadSnapshot& snapshot) {
    if (!snapshot.valid()) {
        return 0;
    }

    // Zero-copy access to ECS columns via Span
    auto transforms = snapshot.transforms();
    auto materialIds = snapshot.materialIds();
    auto meshIds = snapshot.meshIds();
    auto visibility = snapshot.visibility();
    auto flags = snapshot.flags();

    size_t entityCount = snapshot.entityCount();
    size_t writeIdx = 0;

    // Ensure capacity (pre-allocated, just bounds check)
    if (entityCount > instances_.capacity()) {
        // This shouldn't happen with proper MAX_RENDER_INSTANCES
        LOG_ERROR("ECSBridge", "Entity count %zu exceeds max instances %zu",
                 entityCount, instances_.capacity());
        entityCount = instances_.capacity();
    }

    // Ensure size for writing
    if (instances_.size() < entityCount) {
        instances_.resize(entityCount);
    }

    // Extract visible, active entities
    for (size_t i = 0; i < entityCount; ++i) {
        // Check visibility (from frustum culling kernel)
        if (visibility.valid() && visibility[i] == 0) {
            continue;
        }

        // Check active flag
        if (flags.valid() && !ecs::hasFlag(flags[i], ecs::EntityFlags::Active)) {
            continue;
        }

        // Pack into GPUInstanceData
        ecs::GPUInstanceData& inst = instances_[writeIdx++];
        inst.transform = transforms.valid() ? transforms[i] : glm::mat4(1.0f);
        inst.materialId = materialIds.valid() ? materialIds[i] : 0;
        inst.meshId = meshIds.valid() ? meshIds[i] : 0;
        inst.entityIndex = static_cast<uint32_t>(i);
        inst.flags = flags.valid() ? static_cast<uint32_t>(flags[i]) : 0;
    }

    instanceCount_ = writeIdx;
    return instanceCount_;
}

size_t RenderFrameBuilder::extractFromBatch(const ecs::RenderBatch& batch) {
    if (!batch.valid()) {
        return 0;
    }

    // Use batch's pre-filtered visible count
    size_t maxInstances = std::min(batch.visibleCount, instances_.capacity());

    if (instances_.size() < maxInstances) {
        instances_.resize(maxInstances);
    }

    // Use batch's prepareGPUInstances (already filters visibility)
    instanceCount_ = ecs::prepareGPUInstances(batch, ecs::Span<ecs::GPUInstanceData>(instances_));
    return instanceCount_;
}

void RenderFrameBuilder::filterVisible() {
    // Already filtered during extraction - this is a no-op
    // Kept for API consistency with Arrow pattern
}

void RenderFrameBuilder::sortByMaterial() {
    if (instanceCount_ <= 1) {
        return;
    }

    // Sort by materialId to minimize pipeline state changes
    std::sort(instances_.begin(), instances_.begin() + instanceCount_,
              [](const ecs::GPUInstanceData& a, const ecs::GPUInstanceData& b) {
                  // Primary: material ID (most expensive state change)
                  if (a.materialId != b.materialId) {
                      return a.materialId < b.materialId;
                  }
                  // Secondary: mesh ID (for potential batching)
                  return a.meshId < b.meshId;
              });
}

RenderFrame RenderFrameBuilder::finalize(vulkan::VulkanBuffer* gpuBuffer,
                                         vulkan::VulkanBuffer* stagingBuffer) {
    RenderFrame frame;
    frame.instances = instances_.data();
    frame.instanceCount = instanceCount_;
    frame.generation = generation_;
    frame.gpuBuffer = gpuBuffer;
    frame.stagingBuffer = stagingBuffer;
    frame.uploadPending = false;
    frame.onDevice = false;

    // Copy to staging buffer if we have data
    if (instanceCount_ > 0 && stagingBuffer) {
        if (stagingBuffer->upload(instances_.data(),
                                  instanceCount_ * sizeof(ecs::GPUInstanceData))) {
            // Data is now in staging buffer, ready for async upload
        } else {
            LOG_ERROR("ECSBridge", "Failed to upload to staging buffer");
        }
    }

    return frame;
}

// ============================================================================
// FrameRing Implementation
// ============================================================================

bool FrameRing::initialize(VkDevice device, VmaAllocator allocator,
                           uint32_t maxInstances) {
    device_ = device;
    allocator_ = allocator;
    maxInstances_ = maxInstances;

    VkDeviceSize bufferSize = maxInstances * sizeof(ecs::GPUInstanceData);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        auto& frame = frames_[i];

        // Allocate buffer wrappers
        frame.instanceBuffer = std::make_unique<vulkan::VulkanBuffer>();
        frame.stagingBuffer = std::make_unique<vulkan::VulkanBuffer>();

        // Create device-local instance buffer (SSBO)
        if (!frame.instanceBuffer->create(allocator, bufferSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY)) {
            LOG_ERROR("ECSBridge", "Failed to create instance buffer for frame %u", i);
            destroy();
            return false;
        }

        // Create host-visible staging buffer
        if (!frame.stagingBuffer->create(allocator, bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU,
                VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
            LOG_ERROR("ECSBridge", "Failed to create staging buffer for frame %u", i);
            destroy();
            return false;
        }

        // Create upload fence
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled

        if (vkCreateFence(device, &fenceInfo, nullptr, &frame.uploadFence) != VK_SUCCESS) {
            LOG_ERROR("ECSBridge", "Failed to create upload fence for frame %u", i);
            destroy();
            return false;
        }

        frame.initialized = true;
    }

    LOG_INFO("ECSBridge", "Initialized FrameRing with %u frames, %u max instances",
             FRAMES_IN_FLIGHT, maxInstances);
    return true;
}

void FrameRing::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    // Wait for all work to complete
    vkDeviceWaitIdle(device_);

    for (auto& frame : frames_) {
        if (frame.uploadFence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame.uploadFence, nullptr);
            frame.uploadFence = VK_NULL_HANDLE;
        }
        if (frame.instanceBuffer) {
            frame.instanceBuffer->destroy();
            frame.instanceBuffer.reset();
        }
        if (frame.stagingBuffer) {
            frame.stagingBuffer->destroy();
            frame.stagingBuffer.reset();
        }
        frame.initialized = false;
    }

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

FrameRing::PerFrame& FrameRing::acquire() {
    auto& frame = frames_[currentFrame_];

    // Wait for previous upload to complete (should be done by now)
    if (frame.uploadFence != VK_NULL_HANDLE) {
        vkWaitForFences(device_, 1, &frame.uploadFence, VK_TRUE, UINT64_MAX);
        vkResetFences(device_, 1, &frame.uploadFence);
    }

    return frame;
}

const FrameRing::PerFrame& FrameRing::getFrame(uint32_t index) const {
    return frames_[index % FRAMES_IN_FLIGHT];
}

// ============================================================================
// ECSRenderer Implementation
// ============================================================================

bool ECSRenderer::initialize(VkDevice device, VmaAllocator allocator,
                            uint32_t maxInstances) {
    return frameRing_.initialize(device, allocator, maxInstances);
}

void ECSRenderer::destroy() {
    frameRing_.destroy();
}

RenderFrame ECSRenderer::prepareFrame(const ecs::World& world) {
    // Acquire current frame's GPU resources
    auto& perFrame = frameRing_.acquire();

    // Lock-free snapshot acquisition
    ecs::ReadSnapshot snapshot = world.acquireReadSnapshot();

    // Build render frame
    frameBuilder_.beginFrame(snapshot.generation());
    frameBuilder_.extractFromSnapshot(snapshot);
    frameBuilder_.sortByMaterial();

    // Finalize with GPU buffer references
    RenderFrame frame = frameBuilder_.finalize(
        perFrame.instanceBuffer.get(),
        perFrame.stagingBuffer.get());

    lastInstanceCount_ = frame.instanceCount;

    return frame;
}

void ECSRenderer::uploadFrame(RenderFrame& frame, VkCommandBuffer cmd) {
    frame.uploadAsync(cmd);
}

VkDescriptorBufferInfo ECSRenderer::getCurrentBufferInfo() const {
    VkDescriptorBufferInfo info = {};
    const auto& frame = frameRing_.getFrame(frameRing_.currentFrameIndex());
    if (frame.instanceBuffer) {
        info.buffer = frame.instanceBuffer->getBuffer();
        info.offset = 0;
        info.range = lastInstanceCount_ * sizeof(ecs::GPUInstanceData);
    }
    return info;
}

} // namespace jupiter::rendering
