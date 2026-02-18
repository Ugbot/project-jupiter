#pragma once

/**
 * @file marching_cubes.h
 * @brief Marching Cubes mesher for smooth voxel terrain
 *
 * Based on godot_voxel's implementation with:
 * - Gradient-based smooth normals
 * - Vertex reuse cache for reduced vertex count
 * - Edge clamping margin for visual quality
 */

#include "voxel_column.h"
#include "smooth_vertex.h"
#include "transvoxel_tables.h"
#include "mesh_mode.h"
#include <glm/glm.hpp>
#include <array>
#include <unordered_map>

namespace jupiter {
namespace voxel {

/**
 * @brief Vertex reuse cell for caching vertices between adjacent cells
 *
 * Based on Transvoxel paper: vertices on shared edges can be reused.
 */
struct ReuseCell {
    std::array<int32_t, 4> vertices;  // Vertex indices (-1 = not used)
    
    ReuseCell() {
        vertices.fill(-1);
    }
};

/**
 * @brief Vertex cache for Marching Cubes reuse optimization
 *
 * Uses a 2-deck cache (current Z and previous Z) to enable vertex sharing.
 */
class VertexCache {
public:
    void reset(int sizeX, int sizeY) {
        sizeX_ = sizeX;
        sizeY_ = sizeY;
        const size_t deckSize = static_cast<size_t>(sizeX * sizeY);
        for (auto& deck : cache_) {
            deck.resize(deckSize);
            for (auto& cell : deck) {
                cell.vertices.fill(-1);
            }
        }
    }
    
    ReuseCell& get(int x, int y, int z) {
        int deckIdx = z & 1;
        int cellIdx = y * sizeX_ + x;
        return cache_[deckIdx][static_cast<size_t>(cellIdx)];
    }
    
private:
    std::array<std::vector<ReuseCell>, 2> cache_;
    int sizeX_ = 0;
    int sizeY_ = 0;
};

/**
 * @brief Marching Cubes mesher for smooth terrain
 *
 * Implements optimizations from godot_voxel:
 * - Gradient normals from SDF field
 * - Vertex reuse between cells
 * - Edge clamping margin
 */
class MarchingCubesMesher {
public:
    /// Edge clamping margins to prevent vertices at exact corners
    static constexpr float EDGE_CLAMP_MIN = 0.001f;
    static constexpr float EDGE_CLAMP_MAX = 0.999f;
    
    MarchingCubesMesher() = default;
    ~MarchingCubesMesher() = default;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    void setIsoLevel(float level) { isoLevel_ = level; }
    void setLODLevel(LODLevel lod) {
        lod_ = lod;
        stride_ = getLODStride(lod);
    }
    void setSmoothNormals(bool smooth) { smoothNormals_ = smooth; }
    void setVertexReuse(bool enable) { vertexReuse_ = enable; }
    
    // ========================================================================
    // Meshing
    // ========================================================================
    
    void process(const ChunkColumns& chunk,
                 const ChunkColumns* neighbors[6],
                 const ChunkCoord& chunkCoord,
                 SmoothMeshBuffer& output);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    uint32_t getCellsProcessed() const { return cellsProcessed_; }
    uint32_t getTrianglesGenerated() const { return trianglesGenerated_; }
    uint32_t getVerticesReused() const { return verticesReused_; }
    
    void resetStats() {
        cellsProcessed_ = 0;
        trianglesGenerated_ = 0;
        verticesReused_ = 0;
    }
    
private:
    struct CellData {
        float densities[8];
        glm::vec3 gradients[8];
        BlockType materials[8];
        glm::vec3 cellOrigin;
        int cellX, cellY, cellZ;
    };
    
    void processCell(const CellData& cell, SmoothMeshBuffer& output);
    
    uint8_t computeCaseIndex(const float densities[8]) const;
    
    glm::vec3 interpolateEdge(const glm::vec3& p0, const glm::vec3& p1,
                              float d0, float d1) const;
    
    glm::vec3 interpolateNormal(const glm::vec3& n0, const glm::vec3& n1,
                                 float d0, float d1) const;
    
    float sampleDensity(const ChunkColumns& chunk,
                        const ChunkColumns* neighbors[6],
                        int x, int y, int z) const;
    
    glm::vec3 computeGradient(const ChunkColumns& chunk,
                               const ChunkColumns* neighbors[6],
                               int x, int y, int z) const;
    
    BlockType sampleMaterial(const ChunkColumns& chunk,
                             const ChunkColumns* neighbors[6],
                             int x, int y, int z) const;
    
    BlockType getDominantMaterial(const BlockType materials[8],
                                  const float densities[8]) const;
    
    // Configuration
    float isoLevel_ = 0.0f;
    LODLevel lod_ = LODLevel::Full;
    int stride_ = 1;
    bool smoothNormals_ = true;
    bool vertexReuse_ = true;
    
    // Caches (reset per chunk)
    VertexCache vertexCache_;
    const ChunkColumns* currentChunk_ = nullptr;
    const ChunkColumns* const* currentNeighbors_ = nullptr;
    
    // Statistics
    uint32_t cellsProcessed_ = 0;
    uint32_t trianglesGenerated_ = 0;
    uint32_t verticesReused_ = 0;
};

} // namespace voxel
} // namespace jupiter
