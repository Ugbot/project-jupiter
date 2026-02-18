#pragma once

/**
 * @file chunk_columns_pool.h
 * @brief Pre-allocated pool for ChunkColumns to avoid heap allocation per chunk
 *
 * Critical for performance: allocating 200KB ChunkColumns on every chunk load
 * was causing massive slowdowns. This pool pre-allocates all memory at startup
 * and provides O(1) acquire/release operations.
 */

#include "voxel_column.h"
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

/**
 * @brief Pre-allocated pool of ChunkColumns
 *
 * Eliminates heap allocation during chunk loading by pre-allocating
 * all ChunkColumns at initialization. Uses a free list for O(1) operations.
 *
 * Memory layout:
 * - storage_: contiguous array of ChunkColumns (cache friendly)
 * - freeList_: indices of available slots
 * - indexMap_: maps ChunkColumns* back to pool index
 */
class ChunkColumnsPool {
public:
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;
    
    ChunkColumnsPool() = default;
    ~ChunkColumnsPool() = default;
    
    // Non-copyable (owns large memory block)
    ChunkColumnsPool(const ChunkColumnsPool&) = delete;
    ChunkColumnsPool& operator=(const ChunkColumnsPool&) = delete;
    
    // Movable
    ChunkColumnsPool(ChunkColumnsPool&&) = default;
    ChunkColumnsPool& operator=(ChunkColumnsPool&&) = default;
    
    /**
     * @brief Initialize pool with capacity
     *
     * Pre-allocates all ChunkColumns. This is the only allocation.
     *
     * @param maxChunks Maximum number of chunks to support
     */
    void initialize(size_t maxChunks) {
        capacity_ = maxChunks;
        storage_.resize(maxChunks);
        
        // Initialize free list (all slots available)
        freeList_.reserve(maxChunks);
        for (size_t i = 0; i < maxChunks; ++i) {
            freeList_.push_back(static_cast<uint32_t>(maxChunks - 1 - i));  // Reverse order for stack behavior
        }
        
        allocatedCount_ = 0;
    }
    
    /**
     * @brief Acquire a ChunkColumns from the pool
     *
     * O(1) operation - pops from free list.
     *
     * @return Pointer to ChunkColumns, or nullptr if pool exhausted
     */
    ChunkColumns* acquire() {
        if (freeList_.empty()) {
            return nullptr;
        }
        
        uint32_t index = freeList_.back();
        freeList_.pop_back();
        allocatedCount_++;
        
        // Clear the chunk for reuse
        storage_[index].clear();
        
        return &storage_[index];
    }
    
    /**
     * @brief Acquire with index output
     *
     * @param outIndex Output parameter for the pool index
     * @return Pointer to ChunkColumns
     */
    ChunkColumns* acquireWithIndex(uint32_t& outIndex) {
        if (freeList_.empty()) {
            outIndex = INVALID_INDEX;
            return nullptr;
        }
        
        outIndex = freeList_.back();
        freeList_.pop_back();
        allocatedCount_++;
        
        storage_[outIndex].clear();
        
        return &storage_[outIndex];
    }
    
    /**
     * @brief Release a ChunkColumns back to the pool
     *
     * O(1) operation - pushes to free list.
     *
     * @param chunk Pointer to ChunkColumns to release
     */
    void release(ChunkColumns* chunk) {
        if (!chunk) return;
        
        // Calculate index from pointer arithmetic
        size_t index = chunk - storage_.data();
        if (index >= capacity_) {
            return;  // Not from this pool
        }
        
        freeList_.push_back(static_cast<uint32_t>(index));
        allocatedCount_--;
    }
    
    /**
     * @brief Release by index
     *
     * @param index Pool index to release
     */
    void releaseByIndex(uint32_t index) {
        if (index >= capacity_) return;
        
        freeList_.push_back(index);
        allocatedCount_--;
    }
    
    /**
     * @brief Get ChunkColumns by index
     */
    ChunkColumns& at(uint32_t index) {
        return storage_[index];
    }
    
    const ChunkColumns& at(uint32_t index) const {
        return storage_[index];
    }
    
    /**
     * @brief Get index for a ChunkColumns pointer
     */
    uint32_t indexOf(const ChunkColumns* chunk) const {
        if (!chunk) return INVALID_INDEX;
        size_t index = chunk - storage_.data();
        if (index >= capacity_) return INVALID_INDEX;
        return static_cast<uint32_t>(index);
    }
    
    /**
     * @brief Get number of allocated chunks
     */
    uint32_t getAllocatedCount() const {
        return allocatedCount_;
    }
    
    /**
     * @brief Get number of available chunks
     */
    uint32_t getAvailableCount() const {
        return static_cast<uint32_t>(freeList_.size());
    }
    
    /**
     * @brief Get total capacity
     */
    size_t getCapacity() const {
        return capacity_;
    }
    
    /**
     * @brief Check if pool is exhausted
     */
    bool isFull() const {
        return freeList_.empty();
    }
    
    /**
     * @brief Check if pool is initialized
     */
    bool isInitialized() const {
        return capacity_ > 0;
    }

private:
    std::vector<ChunkColumns> storage_;      ///< Pre-allocated chunks
    std::vector<uint32_t> freeList_;          ///< Available slot indices (stack)
    size_t capacity_ = 0;                     ///< Total slots
    uint32_t allocatedCount_ = 0;             ///< Currently allocated
};

} // namespace voxel
} // namespace jupiter



