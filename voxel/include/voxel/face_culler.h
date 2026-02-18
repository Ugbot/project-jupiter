#pragma once

/**
 * @file face_culler.h
 * @brief Hidden face culling for voxel meshing
 *
 * Determines which faces of solid blocks are visible (adjacent to air)
 * and need to be rendered.
 */

#include "voxel_types.h"
#include "voxel_column.h"
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

// ============================================================================
// Face Indices
// ============================================================================

/**
 * @brief Face direction indices
 */
enum FaceDirection : uint8_t {
    FACE_POS_X = 0,  // +X (East)
    FACE_NEG_X = 1,  // -X (West)
    FACE_POS_Y = 2,  // +Y (Up)
    FACE_NEG_Y = 3,  // -Y (Down)
    FACE_POS_Z = 4,  // +Z (South)
    FACE_NEG_Z = 5,  // -Z (North)
    FACE_COUNT = 6,
};

/**
 * @brief Opposite face lookup
 */
constexpr FaceDirection OPPOSITE_FACE[6] = {
    FACE_NEG_X,  // Opposite of +X is -X
    FACE_POS_X,  // Opposite of -X is +X
    FACE_NEG_Y,  // Opposite of +Y is -Y
    FACE_POS_Y,  // Opposite of -Y is +Y
    FACE_NEG_Z,  // Opposite of +Z is -Z
    FACE_POS_Z,  // Opposite of -Z is +Z
};

/**
 * @brief Face normal vectors
 */
constexpr int FACE_NORMALS[6][3] = {
    { 1,  0,  0},  // +X
    {-1,  0,  0},  // -X
    { 0,  1,  0},  // +Y
    { 0, -1,  0},  // -Y
    { 0,  0,  1},  // +Z
    { 0,  0, -1},  // -Z
};

// ============================================================================
// VisibleFace
// ============================================================================

/**
 * @brief A single visible face to be meshed
 */
struct VisibleFace {
    uint8_t x;          ///< Local X position (0-15)
    uint8_t y;          ///< Local Y position (0-127)
    uint8_t z;          ///< Local Z position (0-15)
    uint8_t face;       ///< Face direction (0-5)
    BlockType block;    ///< Block type for material lookup
    uint8_t padding[3];
};

static_assert(sizeof(VisibleFace) == 8, "VisibleFace should be 8 bytes");

// ============================================================================
// FaceCuller
// ============================================================================

/**
 * @brief Determines visible faces in a chunk by checking neighbor blocks
 *
 * A face is visible if:
 * - The block is solid (non-air)
 * - The adjacent block in the face direction is air (or transparent)
 *
 * This class handles neighbor chunks for faces at chunk boundaries.
 */
class FaceCuller {
public:
    FaceCuller() = default;
    ~FaceCuller() = default;
    
    // ========================================================================
    // Processing
    // ========================================================================
    
    /**
     * @brief Process a chunk and determine all visible faces
     *
     * @param chunk The chunk to process
     * @param neighbors Array of 6 neighbor chunks (+X, -X, +Y, -Y, +Z, -Z)
     *                  Can be nullptr for edge chunks
     */
    void process(const ChunkColumns& chunk, const ChunkColumns* neighbors[6]);
    
    /**
     * @brief Process using flat ChunkVoxelData layout
     */
    void processFlat(const ChunkVoxelData& chunk,
                     const ChunkVoxelData* neighbors[6]);
    
    // ========================================================================
    // Results
    // ========================================================================
    
    /**
     * @brief Get the visible faces after processing
     */
    const std::vector<VisibleFace>& visibleFaces() const {
        return visibleFaces_;
    }
    
    /**
     * @brief Get mutable reference to visible faces
     */
    std::vector<VisibleFace>& visibleFaces() {
        return visibleFaces_;
    }
    
    /**
     * @brief Get count of visible faces
     */
    size_t faceCount() const {
        return visibleFaces_.size();
    }
    
    /**
     * @brief Clear visible faces (prepare for new chunk)
     */
    void clear() {
        visibleFaces_.clear();
    }
    
    /**
     * @brief Reserve space for expected face count
     */
    void reserve(size_t count) {
        visibleFaces_.reserve(count);
    }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set whether a block type is transparent
     *
     * Transparent blocks don't occlude faces. By default, only BLOCK_AIR
     * is transparent.
     */
    void setTransparent(BlockType block, bool transparent) {
        transparentMask_[block] = transparent ? 1 : 0;
    }
    
    /**
     * @brief Check if a block type is transparent
     */
    bool isTransparent(BlockType block) const {
        return transparentMask_[block] != 0;
    }
    
private:
    /**
     * @brief Check if neighbor at offset is transparent
     */
    bool isNeighborTransparent(const ChunkColumns& chunk,
                               const ChunkColumns* neighbors[6],
                               int x, int y, int z,
                               FaceDirection face) const;
    
    /**
     * @brief Check if neighbor is transparent (flat layout)
     */
    bool isNeighborTransparentFlat(const ChunkVoxelData& chunk,
                                   const ChunkVoxelData* neighbors[6],
                                   int x, int y, int z,
                                   FaceDirection face) const;
    
    /// Output visible faces
    std::vector<VisibleFace> visibleFaces_;
    
    /// Transparency mask per block type (default: only air is transparent)
    uint8_t transparentMask_[256] = {1};  // Index 0 (AIR) = transparent
};

} // namespace voxel
} // namespace jupiter

