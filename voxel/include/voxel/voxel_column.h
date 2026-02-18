#pragma once

/**
 * @file voxel_column.h
 * @brief Columnar storage for voxel data
 *
 * Organizes voxel data in vertical columns for better cache locality
 * during vertical operations and column-based streaming.
 */

#include "voxel_types.h"
#include <ecs/span.h>
#include <array>
#include <cstring>
#include <algorithm>

namespace jupiter {
namespace voxel {

// ============================================================================
// VoxelColumn - Single Vertical Column
// ============================================================================

/**
 * @brief A single vertical column of voxels at an XZ position
 *
 * Contains all Y values for a single XZ coordinate.
 * Optimized for vertical traversal and height-based operations.
 *
 * Memory layout:
 * - blocks[0] = lowest Y (y=0)
 * - blocks[CHUNK_HEIGHT-1] = highest Y (y=127)
 *
 * Note: Density data for smooth meshing is stored separately in ChunkDensity
 * to reduce memory usage when only blocky mode is needed.
 */
struct alignas(64) VoxelColumn {
    /// Block types for each Y level (material ID)
    std::array<BlockType, CHUNK_HEIGHT> blocks;
    
    /// Lighting/AO data for each Y level
    std::array<uint8_t, CHUNK_HEIGHT> lighting;
    
    /// Minimum Y with solid block (-1 if empty)
    int16_t minSolidY = -1;
    
    /// Maximum Y with solid block (-1 if empty)
    int16_t maxSolidY = -1;
    
    /// Column flags
    uint16_t flags = 0;
    
    /// Reserved for alignment
    uint16_t reserved = 0;
    
    // ========================================================================
    // Access
    // ========================================================================
    
    /**
     * @brief Get block at Y level
     */
    BlockType getBlock(int y) const {
        return blocks[y];
    }
    
    /**
     * @brief Set block at Y level
     */
    void setBlock(int y, BlockType block) {
        blocks[y] = block;
    }
    
    /**
     * @brief Check if Y level is solid (non-air)
     */
    bool isSolidAt(int y) const {
        return blocks[y] != BLOCK_AIR;
    }
    
    /**
     * @brief Get lighting at Y level
     */
    uint8_t getLighting(int y) const {
        return lighting[y];
    }
    
    /**
     * @brief Set lighting at Y level
     */
    void setLighting(int y, uint8_t light) {
        lighting[y] = light;
    }
    
    // ========================================================================
    // Bulk Operations
    // ========================================================================
    
    /**
     * @brief Fill entire column with a block type
     */
    void fill(BlockType block) {
        std::fill(blocks.begin(), blocks.end(), block);
        if (block != BLOCK_AIR) {
            minSolidY = 0;
            maxSolidY = CHUNK_HEIGHT - 1;
        } else {
            minSolidY = -1;
            maxSolidY = -1;
        }
    }
    
    /**
     * @brief Clear column to air
     */
    void clear() {
        fill(BLOCK_AIR);
        std::fill(lighting.begin(), lighting.end(), 63);  // Max light for air
    }
    
    /**
     * @brief Fill range with block type
     */
    void fillRange(int minY, int maxY, BlockType block) {
        for (int y = minY; y <= maxY && y < CHUNK_HEIGHT; ++y) {
            blocks[y] = block;
        }
        updateBounds();
    }
    
    // ========================================================================
    // Height Bounds
    // ========================================================================
    
    /**
     * @brief Update min/max solid Y bounds
     *
     * Call after modifying blocks to update the height cache.
     */
    void updateBounds() {
        minSolidY = -1;
        maxSolidY = -1;
        
        // Find first solid from bottom
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            if (blocks[y] != BLOCK_AIR) {
                minSolidY = static_cast<int16_t>(y);
                break;
            }
        }
        
        if (minSolidY < 0) return;  // Column is empty
        
        // Find last solid from top
        for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
            if (blocks[y] != BLOCK_AIR) {
                maxSolidY = static_cast<int16_t>(y);
                break;
            }
        }
    }
    
    /**
     * @brief Check if column is entirely empty
     */
    bool isEmpty() const {
        return minSolidY < 0;
    }
    
    /**
     * @brief Get height of highest solid block (for heightmap)
     */
    int getHeight() const {
        return maxSolidY;
    }
    
    /**
     * @brief Get solid block count
     */
    int getSolidCount() const {
        int count = 0;
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            if (blocks[y] != BLOCK_AIR) ++count;
        }
        return count;
    }
};

// VoxelColumn size depends on alignment (64-byte aligned for cache efficiency)
// New size: ~264 bytes (was ~776 bytes with density)

// ============================================================================
// ChunkDensity - Optional Density Data for Smooth Meshing
// ============================================================================

/**
 * @brief Separate density storage for smooth terrain meshing
 *
 * Only allocated when MeshMode::Smooth is used. This keeps VoxelColumn
 * small (~264 bytes) for blocky mode while supporting smooth terrain.
 *
 * Memory: 128 floats * 256 columns = 131KB per chunk
 */
struct ChunkDensity {
    /// Density values per column [columnIndex][y]
    std::array<std::array<float, CHUNK_HEIGHT>, CHUNK_SIZE * CHUNK_SIZE> data;
    
    /**
     * @brief Get density at position
     * @note This does NOT handle out-of-bounds - use ChunkColumns::getDensity instead
     */
    float get(int x, int y, int z) const {
        // Bounds checking to prevent buffer overflow
        if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || 
            y < 0 || y >= CHUNK_HEIGHT) {
            return 1.0f;  // Out of bounds = empty
        }
        return data[x + z * CHUNK_SIZE][y];
    }
    
    /**
     * @brief Set density at position
     */
    void set(int x, int y, int z, float d) {
        data[x + z * CHUNK_SIZE][y] = d;
    }
    
    /**
     * @brief Get column density array
     */
    const std::array<float, CHUNK_HEIGHT>& column(int x, int z) const {
        return data[x + z * CHUNK_SIZE];
    }
    
    std::array<float, CHUNK_HEIGHT>& column(int x, int z) {
        return data[x + z * CHUNK_SIZE];
    }
    
    /**
     * @brief Check if voxel is inside surface
     */
    bool isInside(int x, int y, int z) const {
        return data[x + z * CHUNK_SIZE][y] < 0.0f;
    }
    
    /**
     * @brief Clear to empty (all positive = outside)
     */
    void clear() {
        for (auto& col : data) {
            std::fill(col.begin(), col.end(), 1.0f);
        }
    }
};

// ============================================================================
// ChunkColumns - Chunk as Grid of Columns
// ============================================================================

/**
 * @brief A chunk organized as a 16x16 grid of vertical columns
 *
 * Column-major storage for efficient vertical operations:
 * - Streaming loads/unloads entire columns at once
 * - Better cache locality for heightmap operations
 * - SIMD-friendly for per-column operations
 *
 * Access pattern: columns[x + z * CHUNK_SIZE]
 */
class ChunkColumns {
public:
    /// Number of columns in a chunk (16x16 = 256)
    static constexpr size_t COLUMN_COUNT = CHUNK_SIZE * CHUNK_SIZE;
    
    ChunkColumns() = default;
    ~ChunkColumns() = default;
    
    // Move semantics
    ChunkColumns(ChunkColumns&&) = default;
    ChunkColumns& operator=(ChunkColumns&&) = default;
    
    // Copy semantics
    ChunkColumns(const ChunkColumns&) = default;
    ChunkColumns& operator=(const ChunkColumns&) = default;
    
    // ========================================================================
    // Column Access
    // ========================================================================
    
    /**
     * @brief Get column at XZ position
     */
    VoxelColumn& at(int x, int z) {
        return columns_[x + z * CHUNK_SIZE];
    }
    
    const VoxelColumn& at(int x, int z) const {
        return columns_[x + z * CHUNK_SIZE];
    }
    
    /**
     * @brief Get column by linear index
     */
    VoxelColumn& at(size_t index) {
        return columns_[index];
    }
    
    const VoxelColumn& at(size_t index) const {
        return columns_[index];
    }
    
    /**
     * @brief Operator[] for direct column array access
     */
    VoxelColumn& operator[](size_t index) {
        return columns_[index];
    }
    
    const VoxelColumn& operator[](size_t index) const {
        return columns_[index];
    }
    
    // ========================================================================
    // Block Access
    // ========================================================================
    
    /**
     * @brief Get block at local coordinates
     */
    BlockType getBlock(int x, int y, int z) const {
        return columns_[x + z * CHUNK_SIZE].getBlock(y);
    }
    
    /**
     * @brief Set block at local coordinates
     */
    void setBlock(int x, int y, int z, BlockType block) {
        columns_[x + z * CHUNK_SIZE].setBlock(y, block);
        editGeneration_++;
    }
    
    /**
     * @brief Get density at local coordinates (for smooth meshing)
     *
     * Requires density_ to be set via setDensity() or allocateDensity().
     * Returns 1.0 (empty) if no density data.
     */
    float getDensity(int x, int y, int z) const {
        if (!density_) return 1.0f;  // No density = empty
        // Bounds checking - get() now handles this, but we check here too for safety
        if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE || 
            y < 0 || y >= CHUNK_HEIGHT) {
            return 1.0f;  // Out of bounds = empty
        }
        return density_->get(x, y, z);
    }
    
    /**
     * @brief Set density at local coordinates
     *
     * Requires density_ to be set via setDensity() or allocateDensity().
     */
    void setDensity(int x, int y, int z, float d) {
        if (!density_) return;
        density_->set(x, y, z, d);
        editGeneration_++;
    }
    
    /**
     * @brief Check if position is solid (blocky mode)
     */
    bool isSolid(int x, int y, int z) const {
        return columns_[x + z * CHUNK_SIZE].isSolidAt(y);
    }
    
    /**
     * @brief Check if position is inside surface (smooth mode)
     *
     * Uses density if available, otherwise falls back to block check.
     */
    bool isInsideSurface(int x, int y, int z) const {
        if (density_) {
            return density_->get(x, y, z) < 0.0f;
        }
        return columns_[x + z * CHUNK_SIZE].isSolidAt(y);
    }
    
    /**
     * @brief Set external density storage
     */
    void setDensityStorage(ChunkDensity* density) {
        density_ = density;
    }
    
    /**
     * @brief Get density storage
     */
    ChunkDensity* getDensityStorage() { return density_; }
    const ChunkDensity* getDensityStorage() const { return density_; }
    
    /**
     * @brief Check if density data is available
     */
    bool hasDensity() const { return density_ != nullptr; }
    
    // ========================================================================
    // Batch Access
    // ========================================================================
    
    /**
     * @brief Get span of all columns
     */
    ecs::Span<VoxelColumn> allColumns() {
        return ecs::Span<VoxelColumn>(columns_.data(), COLUMN_COUNT);
    }
    
    ecs::Span<const VoxelColumn> allColumns() const {
        return ecs::Span<const VoxelColumn>(columns_.data(), COLUMN_COUNT);
    }
    
    /**
     * @brief Get raw pointer to column array
     */
    VoxelColumn* data() { return columns_.data(); }
    const VoxelColumn* data() const { return columns_.data(); }
    
    // ========================================================================
    // Bulk Operations
    // ========================================================================
    
    /**
     * @brief Fill entire chunk with block type
     */
    void fill(BlockType block) {
        for (auto& col : columns_) {
            col.fill(block);
        }
        editGeneration_++;
    }
    
    /**
     * @brief Clear chunk to air
     */
    void clear() {
        for (auto& col : columns_) {
            col.clear();
        }
        editGeneration_++;
    }
    
    /**
     * @brief Update all column bounds
     */
    void updateAllBounds() {
        for (auto& col : columns_) {
            col.updateBounds();
        }
    }
    
    // ========================================================================
    // Conversion to/from Flat Layout
    // ========================================================================
    
    /**
     * @brief Convert from flat ChunkVoxelData layout
     *
     * The flat layout uses: Z varies fastest, then Y, then X
     * We reorganize to: Y varies fastest within each column
     */
    void fromFlat(const ChunkVoxelData& flat) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                VoxelColumn& col = columns_[x + z * CHUNK_SIZE];
                
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    int flatIdx = ChunkVoxelData::getIndex(x, y, z);
                    col.blocks[y] = flat.blocks[flatIdx];
                    col.lighting[y] = flat.lighting[flatIdx];
                }
                
                col.updateBounds();
            }
        }
        
        editGeneration_ = flat.editGeneration;
    }
    
    /**
     * @brief Convert to flat ChunkVoxelData layout
     */
    void toFlat(ChunkVoxelData& flat) const {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                const VoxelColumn& col = columns_[x + z * CHUNK_SIZE];
                
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    int flatIdx = ChunkVoxelData::getIndex(x, y, z);
                    flat.blocks[flatIdx] = col.blocks[y];
                    flat.lighting[flatIdx] = col.lighting[y];
                }
            }
        }
        
        flat.editGeneration = editGeneration_;
    }
    
    // ========================================================================
    // Generation Tracking
    // ========================================================================
    
    /**
     * @brief Get edit generation counter
     */
    uint64_t getEditGeneration() const {
        return editGeneration_;
    }
    
    /**
     * @brief Increment edit generation (call after modifications)
     */
    void incrementGeneration() {
        editGeneration_++;
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * @brief Get total solid block count
     */
    size_t getSolidCount() const {
        size_t count = 0;
        for (const auto& col : columns_) {
            count += col.getSolidCount();
        }
        return count;
    }
    
    /**
     * @brief Check if chunk is entirely empty
     */
    bool isEmpty() const {
        for (const auto& col : columns_) {
            if (!col.isEmpty()) return false;
        }
        return true;
    }
    
    /**
     * @brief Get heightmap (max Y per column)
     */
    void getHeightmap(int16_t* heights) const {
        for (size_t i = 0; i < COLUMN_COUNT; ++i) {
            heights[i] = columns_[i].maxSolidY;
        }
    }
    
private:
    /// Column storage (16x16 = 256 columns)
    std::array<VoxelColumn, COLUMN_COUNT> columns_;
    
    /// Optional density storage (for smooth meshing)
    /// Not owned by ChunkColumns - managed externally
    ChunkDensity* density_ = nullptr;
    
    /// Edit generation counter
    uint64_t editGeneration_ = 0;
};

// ChunkColumns size depends on VoxelColumn alignment

} // namespace voxel
} // namespace jupiter

