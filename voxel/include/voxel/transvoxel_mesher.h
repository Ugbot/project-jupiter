#pragma once

/**
 * @file transvoxel_mesher.h
 * @brief Transvoxel mesher for LOD boundary stitching
 *
 * Generates transition cells that seamlessly connect meshes
 * of different resolutions (LOD levels).
 *
 * Based on Eric Lengyel's Transvoxel Algorithm.
 * https://transvoxel.org/
 */

#include "voxel_column.h"
#include "smooth_vertex.h"
#include "transvoxel_tables.h"
#include "mesh_mode.h"
#include "face_culler.h"  // For FaceDirection enum
#include <glm/glm.hpp>

namespace jupiter {
namespace voxel {

// Use FaceDirection from face_culler.h

/**
 * @brief Transvoxel mesher for LOD boundary transitions
 *
 * Generates transition cells that connect high-resolution and
 * low-resolution meshes at chunk boundaries.
 */
class TransvoxelMesher {
public:
    TransvoxelMesher() = default;
    ~TransvoxelMesher() = default;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set the iso-level (surface threshold)
     */
    void setIsoLevel(float level) {
        isoLevel_ = level;
    }
    
    // ========================================================================
    // Transition Cell Generation
    // ========================================================================
    
    /**
     * @brief Generate transition cells for a boundary face
     *
     * @param highResChunk Full resolution chunk
     * @param lowResChunk Lower resolution neighbor chunk
     * @param boundaryFace Which face is the boundary
     * @param highResCoord World coordinate of high-res chunk
     * @param output Output mesh buffer
     */
    void processTransition(const ChunkColumns& highResChunk,
                           const ChunkColumns& lowResChunk,
                           FaceDirection boundaryFace,
                           const ChunkCoord& highResCoord,
                           SmoothMeshBuffer& output);
    
    /**
     * @brief Process a single transition cell
     *
     * @param densities 9 high-res + 4 low-res density samples
     * @param materials 9 high-res material samples
     * @param cellOrigin World position of cell origin
     * @param faceDir Which face this transition is on
     * @param output Output mesh buffer
     */
    void processTransitionCell(const float densities[13],
                               const BlockType materials[9],
                               const glm::vec3& cellOrigin,
                               FaceDirection faceDir,
                               SmoothMeshBuffer& output);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    uint32_t getCellsProcessed() const { return cellsProcessed_; }
    uint32_t getTrianglesGenerated() const { return trianglesGenerated_; }
    
    void resetStats() {
        cellsProcessed_ = 0;
        trianglesGenerated_ = 0;
    }
    
private:
    /**
     * @brief Compute the case index from 9 density samples
     */
    uint16_t computeTransitionCaseIndex(const float densities[9]) const;
    
    /**
     * @brief Interpolate vertex on a transition cell edge
     */
    glm::vec3 interpolateTransitionEdge(const float densities[13],
                                         const glm::vec3 positions[13],
                                         int edge) const;
    
    /**
     * @brief Get the 13 sample positions for a transition cell
     */
    void getTransitionPositions(const glm::vec3& cellOrigin,
                                FaceDirection face,
                                float cellSize,
                                glm::vec3 positions[13]) const;
    
    /**
     * @brief Sample density from chunk with face-relative coordinates
     */
    float sampleTransitionDensity(const ChunkColumns& chunk,
                                   FaceDirection face,
                                   int u, int v, int depth,
                                   int lod) const;
    
    /**
     * @brief Compute gradient at a sample position for smooth normals
     */
    glm::vec3 computeTransitionGradient(const ChunkColumns& highResChunk,
                                        const ChunkColumns& lowResChunk,
                                        FaceDirection face,
                                        const glm::vec3& worldPos,
                                        const ChunkCoord& coord) const;
    
    /**
     * @brief Interpolate normal along an edge using gradients
     */
    glm::vec3 interpolateTransitionNormal(const glm::vec3& g0, const glm::vec3& g1,
                                          float d0, float d1) const;
    
    /// Iso-level (typically 0.0)
    float isoLevel_ = 0.0f;
    
    /// Statistics
    uint32_t cellsProcessed_ = 0;
    uint32_t trianglesGenerated_ = 0;
};

} // namespace voxel
} // namespace jupiter

