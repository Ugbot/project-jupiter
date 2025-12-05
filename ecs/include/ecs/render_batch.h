#pragma once

/**
 * @file render_batch.h
 * @brief RenderBatch - Zero-copy rendering view of ECS data
 * 
 * Provides efficient extraction of rendering data from the ECS World
 * for GPU submission. Designed for GPU-driven rendering pipelines.
 */

#include "types.h"
#include "span.h"
#include "simd_ops.h"
#include "world.h"
#include "read_snapshot.h"
#include <glm/glm.hpp>

namespace jupiter::ecs {

/**
 * @brief Rendering data extracted from ECS World
 * 
 * Contains spans pointing directly into ECS column data,
 * enabling zero-copy access for the rendering pipeline.
 * 
 * Thread Safety:
 * - Acquire from a ReadSnapshot for thread-safe access
 * - Valid only while the snapshot is valid (until World::swap)
 */
struct RenderBatch {
    // Transform data
    Span<const glm::mat4> transforms;
    
    // Material/mesh references
    Span<const uint32_t> materialIds;
    Span<const uint32_t> meshIds;
    
    // Visibility (from culling kernel)
    Span<const uint32_t> visibility;
    
    // Bounding volumes (for additional culling)
    Span<const simd::AABB> bounds;
    
    // Entity metadata
    Span<const EntityId> entityIds;
    Span<const EntityFlags> flags;
    
    // Batch metadata
    size_t count = 0;          // Total entity count
    size_t visibleCount = 0;   // Number visible after culling
    uint64_t generation = 0;   // World generation when extracted

    /**
     * @brief Check if batch has valid data
     */
    [[nodiscard]] bool valid() const noexcept {
        return transforms.valid() && count > 0;
    }

    /**
     * @brief Check if entity at index is visible
     */
    [[nodiscard]] bool isVisible(size_t idx) const noexcept {
        return visibility.valid() ? (visibility[idx] != 0) : true;
    }

    /**
     * @brief Check if entity at index is active
     */
    [[nodiscard]] bool isActive(size_t idx) const noexcept {
        return flags.valid() ? hasFlag(flags[idx], EntityFlags::Active) : true;
    }

    /**
     * @brief Iterate over visible entities
     * 
     * Usage:
     *   batch.forEachVisible([](size_t idx, const glm::mat4& xform, uint32_t matId, uint32_t meshId) {
     *       // Submit draw call
     *   });
     */
    template<typename Func>
    void forEachVisible(Func&& fn) const {
        for (size_t i = 0; i < count; ++i) {
            if (isVisible(i) && isActive(i)) {
                fn(i, transforms[i], 
                   materialIds.valid() ? materialIds[i] : 0,
                   meshIds.valid() ? meshIds[i] : 0);
            }
        }
    }

    /**
     * @brief Get visible entity indices for indirect draw
     * 
     * Populates output array with indices of visible entities.
     * @return Number of visible entities written
     */
    size_t getVisibleIndices(Span<uint32_t> output) const {
        size_t writeIdx = 0;
        for (size_t i = 0; i < count && writeIdx < output.size(); ++i) {
            if (isVisible(i) && isActive(i)) {
                output[writeIdx++] = static_cast<uint32_t>(i);
            }
        }
        return writeIdx;
    }
};

/**
 * @brief Extract RenderBatch from a ReadSnapshot (zero-copy)
 * 
 * Thread-safe when used with a valid ReadSnapshot.
 * 
 * @param snapshot The snapshot to extract from
 * @return RenderBatch with spans pointing into snapshot data
 */
inline RenderBatch extractRenderBatch(const ReadSnapshot& snapshot) {
    RenderBatch batch;
    
    if (!snapshot.valid()) {
        return batch;
    }

    batch.transforms = snapshot.transforms();
    batch.materialIds = snapshot.materialIds();
    batch.meshIds = snapshot.meshIds();
    batch.visibility = snapshot.visibility();
    batch.bounds = snapshot.aabbs();
    batch.entityIds = snapshot.ids();
    batch.flags = snapshot.flags();
    batch.count = snapshot.entityCount();
    batch.generation = snapshot.generation();

    // Count visible entities
    if (batch.visibility.valid()) {
        batch.visibleCount = 0;
        for (size_t i = 0; i < batch.count; ++i) {
            if (batch.visibility[i] != 0) {
                ++batch.visibleCount;
            }
        }
    } else {
        batch.visibleCount = batch.count;  // All visible if no culling
    }

    return batch;
}

/**
 * @brief Extract RenderBatch from World's current read buffer
 * 
 * Convenience function for single-threaded use.
 * For multi-threaded rendering, use extractRenderBatch(snapshot) instead.
 */
inline RenderBatch extractRenderBatch(const World& world) {
    return extractRenderBatch(world.acquireReadSnapshot());
}

/**
 * @brief GPU-ready instance data for indirect rendering
 * 
 * Compact structure for uploading to GPU SSBO.
 * 16-byte aligned for efficient GPU access.
 */
struct alignas(16) GPUInstanceData {
    glm::mat4 transform;      // 64 bytes
    uint32_t materialId;      // 4 bytes
    uint32_t meshId;          // 4 bytes
    uint32_t entityIndex;     // 4 bytes
    uint32_t flags;           // 4 bytes
                              // Total: 80 bytes
};

/**
 * @brief Prepare GPU instance data from RenderBatch
 * 
 * Copies visible entity data into GPU-ready format.
 * 
 * @param batch Source render batch
 * @param output Output buffer for GPU data
 * @return Number of instances written
 */
inline size_t prepareGPUInstances(const RenderBatch& batch, Span<GPUInstanceData> output) {
    size_t writeIdx = 0;
    
    for (size_t i = 0; i < batch.count && writeIdx < output.size(); ++i) {
        if (batch.isVisible(i) && batch.isActive(i)) {
            GPUInstanceData& inst = output[writeIdx++];
            inst.transform = batch.transforms[i];
            inst.materialId = batch.materialIds.valid() ? batch.materialIds[i] : 0;
            inst.meshId = batch.meshIds.valid() ? batch.meshIds[i] : 0;
            inst.entityIndex = static_cast<uint32_t>(i);
            inst.flags = batch.flags.valid() ? static_cast<uint32_t>(batch.flags[i]) : 0;
        }
    }
    
    return writeIdx;
}

} // namespace jupiter::ecs

