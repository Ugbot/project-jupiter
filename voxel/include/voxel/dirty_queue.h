#pragma once

#include "voxel_types.h"
#include <memory/queues.h>
#include <atomic>

/**
 * @file dirty_queue.h
 * @brief Lock-free queue for chunks needing re-meshing after edits
 *
 * Following Project Jupiter principles:
 * - Lock-free design using MPMC queue
 * - Deduplication via atomic bitset
 * - No runtime allocations
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Lock-free queue for dirty chunks
 *
 * When voxels are edited, chunks are marked dirty and queued for re-meshing.
 * Uses deduplication to avoid queueing the same chunk multiple times.
 */
class DirtyChunkQueue {
public:
    /// Maximum pending dirty chunks
    static constexpr size_t MAX_DIRTY = 512;

    DirtyChunkQueue() = default;
    ~DirtyChunkQueue() = default;

    // Non-copyable, non-movable
    DirtyChunkQueue(const DirtyChunkQueue&) = delete;
    DirtyChunkQueue& operator=(const DirtyChunkQueue&) = delete;
    DirtyChunkQueue(DirtyChunkQueue&&) = delete;
    DirtyChunkQueue& operator=(DirtyChunkQueue&&) = delete;

    /**
     * @brief Initialize the queue
     */
    void initialize() {
        // Clear dirty bits
        for (size_t i = 0; i < BITSET_SIZE; ++i) {
            dirtyBits_[i].store(0, std::memory_order_relaxed);
        }
        // Drain any existing queue entries
        ChunkCoord dummy;
        while (queue_.pop(dummy)) {}
    }

    /**
     * @brief Mark a chunk as dirty (thread-safe)
     *
     * Uses pool index for deduplication. If already marked dirty,
     * the chunk won't be queued again.
     *
     * @param coord Chunk coordinate
     * @param poolIndex Pool index for deduplication
     * @return true if newly marked dirty, false if already dirty
     */
    bool markDirty(const ChunkCoord& coord, uint32_t poolIndex) {
        if (poolIndex >= MAX_ACTIVE_CHUNKS) {
            return false;
        }

        // Check and set dirty bit atomically
        const size_t wordIndex = poolIndex / 64;
        const uint64_t bitMask = 1ULL << (poolIndex % 64);

        uint64_t oldBits = dirtyBits_[wordIndex].fetch_or(bitMask, std::memory_order_acq_rel);

        if (oldBits & bitMask) {
            // Already dirty, don't queue again
            return false;
        }

        // Newly dirty, queue for meshing
        queue_.push(coord);
        return true;
    }

    /**
     * @brief Pop a dirty chunk from the queue (thread-safe)
     *
     * @param outCoord Output chunk coordinate
     * @return true if a chunk was popped, false if queue empty
     */
    bool pop(ChunkCoord& outCoord) {
        return queue_.pop(outCoord);
    }

    /**
     * @brief Pop multiple dirty chunks (thread-safe)
     *
     * @param outCoords Output array
     * @param maxCount Maximum chunks to pop
     * @return Number of chunks popped
     */
    uint32_t popBatch(ChunkCoord* outCoords, uint32_t maxCount) {
        uint32_t count = 0;
        while (count < maxCount && queue_.pop(outCoords[count])) {
            ++count;
        }
        return count;
    }

    /**
     * @brief Clear dirty bit for a chunk (after meshing completes)
     *
     * @param poolIndex Pool index
     */
    void clearDirtyBit(uint32_t poolIndex) {
        if (poolIndex >= MAX_ACTIVE_CHUNKS) {
            return;
        }

        const size_t wordIndex = poolIndex / 64;
        const uint64_t bitMask = 1ULL << (poolIndex % 64);

        dirtyBits_[wordIndex].fetch_and(~bitMask, std::memory_order_release);
    }

    /**
     * @brief Check if a chunk is marked dirty
     *
     * @param poolIndex Pool index
     * @return true if dirty
     */
    bool isDirty(uint32_t poolIndex) const {
        if (poolIndex >= MAX_ACTIVE_CHUNKS) {
            return false;
        }

        const size_t wordIndex = poolIndex / 64;
        const uint64_t bitMask = 1ULL << (poolIndex % 64);

        return (dirtyBits_[wordIndex].load(std::memory_order_acquire) & bitMask) != 0;
    }

    /**
     * @brief Check if queue is empty
     *
     * @return true if empty
     */
    bool empty() const {
        return queue_.empty();
    }

private:
    static constexpr size_t BITSET_SIZE = (MAX_ACTIVE_CHUNKS + 63) / 64;

    /// Lock-free queue of dirty chunk coordinates
    memory::MPMCQueue<ChunkCoord, MAX_DIRTY> queue_;

    /// Bitset for deduplication (one bit per pool slot)
    alignas(64) std::atomic<uint64_t> dirtyBits_[BITSET_SIZE];
};

/**
 * @brief Mark chunk and neighbors dirty when editing near chunk boundaries
 *
 * Edits at chunk edges require neighboring chunks to also re-mesh
 * because stb_voxel_render needs neighbor data for correct face culling.
 *
 * @param queue Dirty queue
 * @param chunkMap Chunk map for neighbor lookup
 * @param coord Edited chunk coordinate
 * @param poolIndex Edited chunk pool index
 * @param localX Local X coordinate of edit (0 to CHUNK_SIZE-1)
 * @param localY Local Y coordinate of edit
 * @param localZ Local Z coordinate of edit
 * @param getPoolIndex Function to get pool index for a coordinate
 */
template<typename GetPoolIndexFn>
void markDirtyWithNeighbors(
    DirtyChunkQueue& queue,
    const ChunkCoord& coord,
    uint32_t poolIndex,
    int localX, int localY, int localZ,
    GetPoolIndexFn&& getPoolIndex)
{
    // Mark the edited chunk
    queue.markDirty(coord, poolIndex);

    // Check if edit is at chunk boundary and mark neighbors
    if (localX == 0) {
        ChunkCoord neighbor{coord.x - 1, coord.y, coord.z};
        uint32_t idx = getPoolIndex(neighbor);
        if (idx != DirtyChunkQueue::MAX_DIRTY) {
            queue.markDirty(neighbor, idx);
        }
    }
    if (localX == CHUNK_SIZE - 1) {
        ChunkCoord neighbor{coord.x + 1, coord.y, coord.z};
        uint32_t idx = getPoolIndex(neighbor);
        if (idx != DirtyChunkQueue::MAX_DIRTY) {
            queue.markDirty(neighbor, idx);
        }
    }
    if (localY == 0) {
        ChunkCoord neighbor{coord.x, coord.y - 1, coord.z};
        uint32_t idx = getPoolIndex(neighbor);
        if (idx != DirtyChunkQueue::MAX_DIRTY) {
            queue.markDirty(neighbor, idx);
        }
    }
    if (localY == CHUNK_SIZE - 1) {
        ChunkCoord neighbor{coord.x, coord.y + 1, coord.z};
        uint32_t idx = getPoolIndex(neighbor);
        if (idx != DirtyChunkQueue::MAX_DIRTY) {
            queue.markDirty(neighbor, idx);
        }
    }
    if (localZ == 0) {
        ChunkCoord neighbor{coord.x, coord.y, coord.z - 1};
        uint32_t idx = getPoolIndex(neighbor);
        if (idx != DirtyChunkQueue::MAX_DIRTY) {
            queue.markDirty(neighbor, idx);
        }
    }
    if (localZ == CHUNK_SIZE - 1) {
        ChunkCoord neighbor{coord.x, coord.y, coord.z + 1};
        uint32_t idx = getPoolIndex(neighbor);
        if (idx != DirtyChunkQueue::MAX_DIRTY) {
            queue.markDirty(neighbor, idx);
        }
    }
}

} // namespace voxel
} // namespace jupiter
