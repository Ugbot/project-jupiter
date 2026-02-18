#pragma once

/**
 * @file ao_calculator.h
 * @brief Ambient occlusion calculator for voxel vertices
 *
 * Implements Minecraft-style vertex ambient occlusion using
 * the 3-sample corner + side technique.
 */

#include "voxel_types.h"
#include "voxel_column.h"
#include "face_culler.h"
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

// ============================================================================
// Vertex AO Values
// ============================================================================

/**
 * @brief AO values for a single quad's 4 vertices
 *
 * Vertex order: (0,0), (1,0), (1,1), (0,1) in face-local UV space
 */
struct QuadAO {
    uint8_t ao[4];  ///< AO values 0-3 for each vertex (0=dark, 3=bright)
    
    /**
     * @brief Flip the quad if needed to avoid T-junction artifacts
     *
     * When AO values differ diagonally, we may need to flip the quad
     * triangulation to avoid visual seams.
     *
     * @return true if quad should use alternate triangulation
     */
    bool shouldFlip() const {
        // Standard Minecraft AO fix: flip if ao[0]+ao[2] > ao[1]+ao[3]
        return (ao[0] + ao[2]) > (ao[1] + ao[3]);
    }
    
    /**
     * @brief Pack AO values into a single byte (2 bits each)
     */
    uint8_t pack() const {
        return (ao[0] & 0x3) |
               ((ao[1] & 0x3) << 2) |
               ((ao[2] & 0x3) << 4) |
               ((ao[3] & 0x3) << 6);
    }
};

// ============================================================================
// AOCalculator
// ============================================================================

/**
 * @brief Calculates ambient occlusion for visible face vertices
 *
 * Uses the Minecraft-style corner + side sampling:
 * For each vertex of a face, sample the 3 adjacent blocks (2 sides + 1 corner)
 * and compute AO as: ao = side1 + side2 + corner (if both sides occluded)
 *
 * This creates smooth corner darkening while being cheap to compute.
 */
class AOCalculator {
public:
    AOCalculator() = default;
    ~AOCalculator() = default;
    
    // ========================================================================
    // Processing
    // ========================================================================
    
    /**
     * @brief Calculate AO for all visible faces
     *
     * @param chunk The chunk data
     * @param neighbors Neighbor chunks for edge AO
     * @param faces Visible faces from FaceCuller
     */
    void process(const ChunkColumns& chunk,
                 const ChunkColumns* neighbors[6],
                 const std::vector<VisibleFace>& faces);
    
    /**
     * @brief Process using flat layout
     */
    void processFlat(const ChunkVoxelData& chunk,
                     const ChunkVoxelData* neighbors[6],
                     const std::vector<VisibleFace>& faces);
    
    // ========================================================================
    // Results
    // ========================================================================
    
    /**
     * @brief Get AO values for all faces (same order as input faces)
     */
    const std::vector<QuadAO>& quadAO() const {
        return quadAO_;
    }
    
    std::vector<QuadAO>& quadAO() {
        return quadAO_;
    }
    
    /**
     * @brief Clear AO values
     */
    void clear() {
        quadAO_.clear();
    }
    
private:
    /**
     * @brief Calculate AO for a single vertex
     *
     * @param chunk Chunk data
     * @param neighbors Neighbor chunks
     * @param x, y, z Block position
     * @param face Face direction
     * @param vertex Vertex index (0-3)
     * @return AO value 0-3
     */
    uint8_t calculateVertexAO(const ChunkColumns& chunk,
                              const ChunkColumns* neighbors[6],
                              int x, int y, int z,
                              FaceDirection face,
                              int vertex) const;
    
    uint8_t calculateVertexAOFlat(const ChunkVoxelData& chunk,
                                  const ChunkVoxelData* neighbors[6],
                                  int x, int y, int z,
                                  FaceDirection face,
                                  int vertex) const;
    
    /**
     * @brief Check if a block position is occluding (solid)
     */
    bool isOccluding(const ChunkColumns& chunk,
                     const ChunkColumns* neighbors[6],
                     int x, int y, int z) const;
    
    bool isOccludingFlat(const ChunkVoxelData& chunk,
                         const ChunkVoxelData* neighbors[6],
                         int x, int y, int z) const;
    
    /**
     * @brief Get block at potentially out-of-bounds position
     */
    BlockType getBlockAt(const ChunkColumns& chunk,
                         const ChunkColumns* neighbors[6],
                         int x, int y, int z) const;
    
    BlockType getBlockAtFlat(const ChunkVoxelData& chunk,
                             const ChunkVoxelData* neighbors[6],
                             int x, int y, int z) const;
    
    /// Output AO values per quad
    std::vector<QuadAO> quadAO_;
};

} // namespace voxel
} // namespace jupiter



