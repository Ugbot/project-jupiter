#pragma once

#include "voxel_types.h"
#include <memory/queues.h>
#include <atomic>
#include <cstring>

/**
 * @file chunk_pool.h
 * @brief Pre-allocated pool of chunk voxel data
 *
 * Following Project Jupiter principles:
 * - No runtime allocations (pre-allocated storage)
 * - Lock-free allocation/deallocation using MPMC queue
 * - 64-byte alignment for SIMD operations
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Lock-free pool of ChunkVoxelData instances
 *
 * Pre-allocates MAX_ACTIVE_CHUNKS chunks at initialization.
 * Uses MPMC queue for thread-safe allocation/deallocation.
 *
 * Memory usage: MAX_ACTIVE_CHUNKS * sizeof(ChunkVoxelData) = ~48MB
 */
class ChunkPool {
public:
    /// Invalid pool index
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    ChunkPool() = default;
    ~ChunkPool() = default;

    // Non-copyable, non-movable
    ChunkPool(const ChunkPool&) = delete;
    ChunkPool& operator=(const ChunkPool&) = delete;
    ChunkPool(ChunkPool&&) = delete;
    ChunkPool& operator=(ChunkPool&&) = delete;

    /**
     * @brief Initialize the pool
     *
     * Pre-allocates all chunks and populates the free list.
     * Must be called before any allocate/release operations.
     */
    void initialize() {
        // Clear all chunk data
        std::memset(storage_, 0, sizeof(storage_));

        // Populate free list with all indices
        for (uint32_t i = 0; i < MAX_ACTIVE_CHUNKS; ++i) {
            freeList_.push(i);
        }

        allocatedCount_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Shutdown the pool
     *
     * Resets state. Outstanding allocations become invalid.
     */
    void shutdown() {
        // Drain free list
        uint32_t dummy;
        while (freeList_.pop(dummy)) {}

        allocatedCount_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Allocate a chunk from the pool (lock-free)
     *
     * @return Pool index, or INVALID_INDEX if pool is exhausted
     */
    [[nodiscard]] uint32_t allocate() {
        uint32_t index;
        if (!freeList_.pop(index)) {
            return INVALID_INDEX;  // Pool exhausted
        }

        // Clear the chunk data
        storage_[index].clear();
        storage_[index].editGeneration = 0;

        allocatedCount_.fetch_add(1, std::memory_order_relaxed);
        return index;
    }

    /**
     * @brief Release a chunk back to the pool (lock-free)
     *
     * @param index Pool index to release
     */
    void release(uint32_t index) {
        if (index >= MAX_ACTIVE_CHUNKS) {
            return;  // Invalid index
        }

        freeList_.push(index);
        allocatedCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    /**
     * @brief Get chunk data at index
     *
     * @param index Pool index
     * @return Reference to chunk data
     */
    ChunkVoxelData& at(uint32_t index) {
        return storage_[index];
    }

    /**
     * @brief Get chunk data at index (const)
     *
     * @param index Pool index
     * @return Const reference to chunk data
     */
    const ChunkVoxelData& at(uint32_t index) const {
        return storage_[index];
    }

    /**
     * @brief Get current allocation count
     *
     * @return Number of allocated chunks
     */
    uint32_t getAllocatedCount() const {
        return allocatedCount_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get available chunk count
     *
     * @return Number of chunks available for allocation
     */
    uint32_t getAvailableCount() const {
        return MAX_ACTIVE_CHUNKS - getAllocatedCount();
    }

    /**
     * @brief Get pool capacity
     *
     * @return Maximum number of chunks
     */
    constexpr uint32_t capacity() const {
        return MAX_ACTIVE_CHUNKS;
    }

private:
    /// Pre-allocated chunk storage (64-byte aligned)
    alignas(64) ChunkVoxelData storage_[MAX_ACTIVE_CHUNKS];

    /// Lock-free free list
    memory::MPMCQueue<uint32_t, MAX_ACTIVE_CHUNKS> freeList_;

    /// Current allocation count (for debugging/stats)
    std::atomic<uint32_t> allocatedCount_{0};
};

} // namespace voxel
} // namespace jupiter
