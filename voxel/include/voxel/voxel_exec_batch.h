#pragma once

/**
 * @file voxel_exec_batch.h
 * @brief Voxel-specific ExecBatch for kernel execution
 *
 * Type-erased column collection for voxel kernel input/output,
 * mirroring the ECS ExecBatch pattern.
 */

#include "voxel_types.h"
#include <ecs/span.h>
#include <cstddef>
#include <cstdint>
#include <cassert>

namespace jupiter {
namespace voxel {

// Forward declarations
class ChunkColumns;
struct MeshBuffer;

// ============================================================================
// Column Identifiers
// ============================================================================

/**
 * @brief Bitmask for voxel column types in a batch
 */
enum class VoxelColumnId : uint32_t {
    None        = 0,
    Blocks      = 1 << 0,   ///< Block type data (BlockType array)
    Lighting    = 1 << 1,   ///< Lighting/AO data (uint8_t array)
    Dirty       = 1 << 2,   ///< Dirty flags (bool array)
    Neighbors   = 1 << 3,   ///< Neighbor chunk pointers
    MeshBuffer  = 1 << 4,   ///< Output mesh buffer
    VisibleFaces = 1 << 5,  ///< Face visibility mask per voxel
    VertexAO    = 1 << 6,   ///< Per-vertex AO values
    MergedQuads = 1 << 7,   ///< Output from greedy mesher
    
    // Computed columns
    All         = 0xFFFFFFFF,
};

inline VoxelColumnId operator|(VoxelColumnId a, VoxelColumnId b) {
    return static_cast<VoxelColumnId>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline VoxelColumnId operator&(VoxelColumnId a, VoxelColumnId b) {
    return static_cast<VoxelColumnId>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasColumn(VoxelColumnId mask, VoxelColumnId col) {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(col)) != 0;
}

// ============================================================================
// VoxelExecBatch
// ============================================================================

/// Maximum number of columns in a voxel batch
constexpr size_t MAX_VOXEL_COLUMNS = 16;

/**
 * @brief Type-erased batch of voxel columns for kernel execution
 *
 * Similar to ECS ExecBatch, provides uniform interface for kernels
 * to access input/output voxel data without compile-time type knowledge.
 */
struct VoxelExecBatch {
    /// Type-erased column pointers (indexed by VoxelColumnId bit position)
    void* columns[MAX_VOXEL_COLUMNS] = {};
    
    /// Bitmask of which columns are present
    VoxelColumnId presentMask = VoxelColumnId::None;
    
    /// Chunk coordinate this batch operates on
    ChunkCoord chunkCoord;
    
    /// Number of voxels in this batch (typically CHUNK_SIZE^3)
    size_t voxelCount = 0;
    
    /// Batch index for parallel processing
    size_t batchIndex = 0;
    
    /// User data pointer for kernel context
    void* userData = nullptr;
    
    // ========================================================================
    // Column Access
    // ========================================================================
    
    /**
     * @brief Get a typed pointer to a column
     */
    template<typename T>
    T* column(VoxelColumnId id) noexcept {
        const uint32_t idx = columnIndex(id);
        if (idx >= MAX_VOXEL_COLUMNS || !hasColumn(presentMask, id)) {
            return nullptr;
        }
        return static_cast<T*>(columns[idx]);
    }
    
    template<typename T>
    const T* column(VoxelColumnId id) const noexcept {
        const uint32_t idx = columnIndex(id);
        if (idx >= MAX_VOXEL_COLUMNS || !hasColumn(presentMask, id)) {
            return nullptr;
        }
        return static_cast<const T*>(columns[idx]);
    }
    
    /**
     * @brief Get a span view of a column
     */
    template<typename T>
    ecs::Span<T> columnSpan(VoxelColumnId id) noexcept {
        T* ptr = column<T>(id);
        if (!ptr) {
            return ecs::Span<T>();
        }
        return ecs::Span<T>(ptr, voxelCount);
    }
    
    template<typename T>
    ecs::Span<const T> columnSpan(VoxelColumnId id) const noexcept {
        const T* ptr = column<T>(id);
        if (!ptr) {
            return ecs::Span<const T>();
        }
        return ecs::Span<const T>(ptr, voxelCount);
    }
    
    /**
     * @brief Set a column pointer
     */
    template<typename T>
    void setColumn(VoxelColumnId id, T* ptr) noexcept {
        const uint32_t idx = columnIndex(id);
        if (idx < MAX_VOXEL_COLUMNS) {
            columns[idx] = static_cast<void*>(ptr);
            presentMask = presentMask | id;
        }
    }
    
    /**
     * @brief Check if a column is present
     */
    bool has(VoxelColumnId id) const noexcept {
        return hasColumn(presentMask, id);
    }
    
    /**
     * @brief Get raw pointer to column data
     */
    void* rawColumn(VoxelColumnId id) noexcept {
        const uint32_t idx = columnIndex(id);
        return (idx < MAX_VOXEL_COLUMNS) ? columns[idx] : nullptr;
    }
    
    const void* rawColumn(VoxelColumnId id) const noexcept {
        const uint32_t idx = columnIndex(id);
        return (idx < MAX_VOXEL_COLUMNS) ? columns[idx] : nullptr;
    }
    
    // ========================================================================
    // Batch Operations
    // ========================================================================
    
    /**
     * @brief Clear all columns and reset the batch
     */
    void clear() noexcept {
        for (size_t i = 0; i < MAX_VOXEL_COLUMNS; ++i) {
            columns[i] = nullptr;
        }
        presentMask = VoxelColumnId::None;
        chunkCoord = ChunkCoord();
        voxelCount = 0;
        batchIndex = 0;
        userData = nullptr;
    }
    
    /**
     * @brief Check if batch is empty
     */
    bool empty() const noexcept { return voxelCount == 0; }
    
    /**
     * @brief Get the number of columns present
     */
    size_t columnCount() const noexcept {
        uint32_t mask = static_cast<uint32_t>(presentMask);
        size_t count = 0;
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
        return count;
    }
    
private:
    /**
     * @brief Convert VoxelColumnId to array index (bit position)
     */
    static constexpr uint32_t columnIndex(VoxelColumnId id) noexcept {
        uint32_t val = static_cast<uint32_t>(id);
        if (val == 0) return MAX_VOXEL_COLUMNS;  // Invalid
        
        // Find the bit position (log2 of power of 2)
        uint32_t idx = 0;
        while ((val & 1) == 0) {
            val >>= 1;
            ++idx;
        }
        return idx;
    }
};

/**
 * @brief Builder for constructing VoxelExecBatch instances
 */
class VoxelExecBatchBuilder {
public:
    VoxelExecBatchBuilder() = default;
    
    VoxelExecBatchBuilder& setChunkCoord(const ChunkCoord& coord) noexcept {
        batch_.chunkCoord = coord;
        return *this;
    }
    
    VoxelExecBatchBuilder& setVoxelCount(size_t count) noexcept {
        batch_.voxelCount = count;
        return *this;
    }
    
    VoxelExecBatchBuilder& setBatchIndex(size_t idx) noexcept {
        batch_.batchIndex = idx;
        return *this;
    }
    
    VoxelExecBatchBuilder& setUserData(void* ptr) noexcept {
        batch_.userData = ptr;
        return *this;
    }
    
    template<typename T>
    VoxelExecBatchBuilder& addColumn(VoxelColumnId id, T* data) noexcept {
        batch_.setColumn(id, data);
        return *this;
    }
    
    VoxelExecBatch build() noexcept {
        return batch_;
    }
    
private:
    VoxelExecBatch batch_;
};

} // namespace voxel
} // namespace jupiter



