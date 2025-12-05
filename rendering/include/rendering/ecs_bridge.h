#pragma once

/**
 * @file ecs_bridge.h
 * @brief GPU-side ECS-to-Renderer bridge (Apache Arrow-inspired)
 *
 * Core Principle: The renderer NEVER waits for simulation.
 *
 * Inspired by:
 * - ECS tick-tock double-buffering (N readers, 1 writer)
 * - Apache Arrow RecordBatch/BufferBuilder patterns
 * - Arrow CUDA zero-copy device abstraction
 *
 * Thread Model:
 * - Simulation thread: writes to ECS World, calls swap()
 * - Render thread: acquires snapshots, uploads to GPU
 * - Both threads run independently, never blocking each other
 *
 * Architecture:
 * - ECS World (ecs/) runs independently - NO renderer dependency
 * - SimulationRunner (ecs/simulation_runner.h) for headless servers
 * - ECSRenderer (this file) bridges ECS to GPU - OPTIONAL
 *
 * Usage (With Rendering):
 * @code
 *   // In Application::onInit()
 *   enableECSRendering(&world, 65536);
 *
 *   // Each frame, renderer automatically:
 *   // 1. Acquires lock-free ECS snapshot
 *   // 2. Extracts visible instances
 *   // 3. Uploads to GPU via staging buffer
 * @endcode
 *
 * For headless operation, use ecs::SimulationRunner instead.
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <cstdint>

// Include ECS types (needed for GPUInstanceData in std::vector)
#include "ecs/render_batch.h"

// Forward declarations (for types only used by pointer/reference)
namespace jupiter::ecs {
    class World;
}

namespace jupiter::rendering {

// Forward declarations
namespace vulkan {
    class VulkanBuffer;
    class VulkanContext;
}

/**
 * @brief Maximum entities supported per frame
 *
 * Pre-allocated to avoid runtime allocation.
 * Following CLAUDE.md: No runtime allocations in hot paths.
 */
constexpr uint32_t MAX_RENDER_INSTANCES = 65536;

/**
 * @brief Number of frames in flight for GPU resource ring
 */
constexpr uint32_t FRAMES_IN_FLIGHT = 2;

// ============================================================================
// RenderFrame - Arrow RecordBatch-style immutable batch
// ============================================================================

/**
 * @brief Immutable batch of render data for a single frame
 *
 * Like Apache Arrow's RecordBatch, this represents a contiguous batch
 * of data ready for GPU submission. Once created, it's immutable.
 *
 * Lifecycle:
 * 1. RenderFrameBuilder extracts from ECS snapshot
 * 2. RenderFrame created with finalize()
 * 3. uploadAsync() queues GPU transfer
 * 4. Used for draw commands
 * 5. Recycled next frame
 */
struct RenderFrame {
    // Instance data (points into staging buffer or CPU vector)
    const ecs::GPUInstanceData* instances = nullptr;
    size_t instanceCount = 0;

    // ECS generation for staleness detection
    uint64_t generation = 0;

    // GPU resources (borrowed from FrameRing)
    vulkan::VulkanBuffer* gpuBuffer = nullptr;      // Device-local SSBO
    vulkan::VulkanBuffer* stagingBuffer = nullptr;  // Host-visible staging

    // State tracking
    bool uploadPending = false;
    bool onDevice = false;

    /**
     * @brief Check if data needs upload to GPU
     */
    [[nodiscard]] bool needsUpload() const noexcept {
        return instanceCount > 0 && !onDevice;
    }

    /**
     * @brief Check if this frame has valid data
     */
    [[nodiscard]] bool valid() const noexcept {
        return instances != nullptr && instanceCount > 0;
    }

    /**
     * @brief Queue async upload from staging to device
     *
     * Like Arrow's Buffer::View() - lazy transfer to device.
     * Does NOT wait for completion.
     */
    void uploadAsync(VkCommandBuffer cmd);

    /**
     * @brief Get descriptor buffer info for binding
     */
    [[nodiscard]] VkDescriptorBufferInfo getBufferInfo() const noexcept;
};

// ============================================================================
// RenderFrameBuilder - Arrow BufferBuilder-style incremental construction
// ============================================================================

/**
 * @brief Incrementally builds a RenderFrame from ECS snapshot
 *
 * Like Apache Arrow's BufferBuilder, this accumulates data with
 * automatic alignment and pre-allocation.
 *
 * Usage:
 *   builder.beginFrame(generation);
 *   builder.extractFromSnapshot(snapshot);
 *   builder.filterVisible();
 *   builder.sortByMaterial();
 *   RenderFrame frame = builder.finalize(gpuBuffer, stagingBuffer);
 */
class RenderFrameBuilder {
public:
    RenderFrameBuilder();
    ~RenderFrameBuilder() = default;

    // Non-copyable, movable
    RenderFrameBuilder(const RenderFrameBuilder&) = delete;
    RenderFrameBuilder& operator=(const RenderFrameBuilder&) = delete;
    RenderFrameBuilder(RenderFrameBuilder&&) = default;
    RenderFrameBuilder& operator=(RenderFrameBuilder&&) = default;

    /**
     * @brief Begin building a new frame
     * @param generation ECS world generation
     */
    void beginFrame(uint64_t generation);

    /**
     * @brief Extract visible instances from ECS snapshot (zero-copy read)
     * @param snapshot ECS read snapshot
     * @return Number of instances extracted
     */
    size_t extractFromSnapshot(const ecs::ReadSnapshot& snapshot);

    /**
     * @brief Extract from RenderBatch (uses existing filtering)
     * @param batch Pre-extracted render batch
     * @return Number of instances extracted
     */
    size_t extractFromBatch(const ecs::RenderBatch& batch);

    /**
     * @brief Filter to only visible entities
     *
     * Call after extract if visibility not pre-filtered.
     */
    void filterVisible();

    /**
     * @brief Sort instances by material for batching
     *
     * Reduces pipeline state changes during rendering.
     */
    void sortByMaterial();

    /**
     * @brief Finalize and create immutable RenderFrame
     *
     * Copies data to staging buffer for GPU upload.
     *
     * @param gpuBuffer Device-local buffer (borrowed)
     * @param stagingBuffer Host-visible staging buffer (borrowed)
     * @return Immutable RenderFrame ready for upload
     */
    RenderFrame finalize(vulkan::VulkanBuffer* gpuBuffer,
                        vulkan::VulkanBuffer* stagingBuffer);

    /**
     * @brief Get current instance count
     */
    [[nodiscard]] size_t instanceCount() const noexcept {
        return instanceCount_;
    }

    /**
     * @brief Clear builder for reuse
     */
    void clear() noexcept;

private:
    // Pre-allocated instance storage (64-byte aligned like Arrow)
    alignas(64) std::vector<ecs::GPUInstanceData> instances_;
    size_t instanceCount_ = 0;
    uint64_t generation_ = 0;
};

// ============================================================================
// FrameRing - Double-buffered GPU resources
// ============================================================================

/**
 * @brief Ring buffer of per-frame GPU resources
 *
 * Provides double-buffered (or N-buffered) GPU resources to ensure
 * render thread never waits for simulation or previous frame.
 *
 * Each frame has:
 * - Device-local instance buffer (SSBO)
 * - Host-visible staging buffer (for async upload)
 * - Fence for upload synchronization
 */
class FrameRing {
public:
    /**
     * @brief Per-frame GPU resources
     *
     * Uses unique_ptr for VulkanBuffer to allow forward declaration
     * (VulkanBuffer is an internal class in vulkan_backend.h).
     */
    struct PerFrame {
        std::unique_ptr<vulkan::VulkanBuffer> instanceBuffer;   // GPU instance SSBO
        std::unique_ptr<vulkan::VulkanBuffer> stagingBuffer;    // CPU→GPU staging
        VkFence uploadFence = VK_NULL_HANDLE;  // Async upload sync
        uint64_t lastGeneration = 0;           // Last uploaded ECS generation
        void* mappedStaging = nullptr;         // Persistently mapped staging

        bool initialized = false;

        PerFrame() = default;
        ~PerFrame();  // Defined in .cpp where VulkanBuffer is complete
        PerFrame(PerFrame&&) noexcept;
        PerFrame& operator=(PerFrame&&) noexcept;

        // Non-copyable
        PerFrame(const PerFrame&) = delete;
        PerFrame& operator=(const PerFrame&) = delete;
    };

    FrameRing() = default;
    ~FrameRing() { destroy(); }

    // Non-copyable
    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    /**
     * @brief Initialize GPU resources
     *
     * Pre-allocates all buffers for MAX_RENDER_INSTANCES.
     * Following CLAUDE.md: No runtime allocations.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param maxInstances Maximum instances per frame
     * @return true if successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator,
                   uint32_t maxInstances = MAX_RENDER_INSTANCES);

    /**
     * @brief Destroy all GPU resources
     */
    void destroy();

    /**
     * @brief Acquire current frame's resources
     *
     * Waits on fence if previous upload still pending (rare).
     */
    PerFrame& acquire();

    /**
     * @brief Advance to next frame
     */
    void advance() noexcept {
        currentFrame_ = (currentFrame_ + 1) % FRAMES_IN_FLIGHT;
    }

    /**
     * @brief Get current frame index
     */
    [[nodiscard]] uint32_t currentFrameIndex() const noexcept {
        return currentFrame_;
    }

    /**
     * @brief Get frame by index (const access)
     */
    [[nodiscard]] const PerFrame& getFrame(uint32_t index) const;

    /**
     * @brief Check if initialized
     */
    [[nodiscard]] bool isInitialized() const noexcept {
        return device_ != VK_NULL_HANDLE;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    std::array<PerFrame, FRAMES_IN_FLIGHT> frames_;
    uint32_t currentFrame_ = 0;
    uint32_t maxInstances_ = 0;
};

// ============================================================================
// ECSRenderer - Main bridge class
// ============================================================================

/**
 * @brief Main ECS-to-Renderer bridge
 *
 * Connects the ECS World to the Vulkan renderer with complete decoupling.
 * Render thread never blocks on simulation, simulation never blocks on render.
 *
 * Usage:
 *   // Initialization
 *   ecsRenderer.initialize(device, allocator, maxInstances);
 *
 *   // Per frame (render thread)
 *   RenderFrame frame = ecsRenderer.prepareFrame(world);
 *   ecsRenderer.uploadFrame(frame, commandBuffer);
 *   // Use frame.getBufferInfo() for descriptor binding
 *   // Draw with frame.instanceCount instances
 */
class ECSRenderer {
public:
    ECSRenderer() = default;
    ~ECSRenderer() { destroy(); }

    // Non-copyable
    ECSRenderer(const ECSRenderer&) = delete;
    ECSRenderer& operator=(const ECSRenderer&) = delete;

    /**
     * @brief Initialize the ECS renderer bridge
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param maxInstances Maximum instances per frame
     * @return true if successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator,
                   uint32_t maxInstances = MAX_RENDER_INSTANCES);

    /**
     * @brief Destroy all resources
     */
    void destroy();

    /**
     * @brief Prepare render frame from ECS world (lock-free)
     *
     * Acquires a snapshot, extracts visible instances, prepares for upload.
     * NEVER blocks on simulation thread.
     *
     * @param world ECS world to read from
     * @return RenderFrame ready for upload
     */
    RenderFrame prepareFrame(const ecs::World& world);

    /**
     * @brief Upload render frame to GPU (async)
     *
     * Copies staging data to device-local buffer.
     * Does NOT wait for completion.
     *
     * @param frame Frame to upload
     * @param cmd Command buffer for transfer commands
     */
    void uploadFrame(RenderFrame& frame, VkCommandBuffer cmd);

    /**
     * @brief Advance to next frame
     *
     * Call after submitting render commands.
     */
    void nextFrame() { frameRing_.advance(); }

    /**
     * @brief Get current frame's instance buffer for binding
     */
    [[nodiscard]] VkDescriptorBufferInfo getCurrentBufferInfo() const;

    /**
     * @brief Get last prepared frame's instance count
     */
    [[nodiscard]] size_t lastInstanceCount() const noexcept {
        return lastInstanceCount_;
    }

    /**
     * @brief Check if initialized
     */
    [[nodiscard]] bool isInitialized() const noexcept {
        return frameRing_.isInitialized();
    }

private:
    FrameRing frameRing_;
    RenderFrameBuilder frameBuilder_;
    size_t lastInstanceCount_ = 0;
};

} // namespace jupiter::rendering

// Note: For headless/server operation, use ecs::SimulationRunner from
// <ecs/simulation_runner.h> which has NO rendering dependencies.
