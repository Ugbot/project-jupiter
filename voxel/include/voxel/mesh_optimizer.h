#pragma once

/**
 * @file mesh_optimizer.h
 * @brief Mesh optimization utilities using meshoptimizer
 *
 * Provides:
 * - Thread-local mesh buffers to avoid allocations
 * - Vertex cache optimization
 * - Mesh simplification with border locking for LOD
 * - Vertex fetch optimization
 */

#include "smooth_vertex.h"
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

/**
 * @brief Thread-local mesh buffers to avoid per-chunk allocations
 *
 * Inspired by godot_voxel's approach of using thread_local storage
 * to reuse buffers across multiple meshing operations.
 */
struct ThreadLocalMeshBuffers {
    std::vector<SmoothVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<unsigned int> remapBuffer;
    
    void clear() {
        vertices.clear();
        indices.clear();
        remapBuffer.clear();
    }
    
    void reserve(size_t vertCapacity, size_t idxCapacity) {
        if (vertices.capacity() < vertCapacity) {
            vertices.reserve(vertCapacity);
        }
        if (indices.capacity() < idxCapacity) {
            indices.reserve(idxCapacity);
        }
    }
};

/**
 * @brief Get thread-local mesh buffers
 *
 * Returns buffers that persist across calls within the same thread,
 * avoiding repeated heap allocations.
 */
ThreadLocalMeshBuffers& getThreadLocalMeshBuffers();

/**
 * @brief Mesh optimization configuration
 */
struct MeshOptConfig {
    bool optimizeVertexCache = true;     ///< Reorder indices for GPU cache
    bool optimizeVertexFetch = true;      ///< Reorder vertices for fetch efficiency
    bool generateLOD = false;             ///< Generate simplified LOD mesh
    float lodErrorThreshold = 0.01f;      ///< Error threshold for simplification
    float lodTargetRatio = 0.5f;          ///< Target vertex count ratio (0.5 = 50%)
    bool lockBorders = true;              ///< Lock chunk borders during simplification
};

/**
 * @brief Mesh optimizer for voxel meshes
 *
 * Uses meshoptimizer library for:
 * - Vertex cache optimization (better GPU performance)
 * - Vertex fetch optimization (reduces memory bandwidth)
 * - Mesh simplification for LOD (with border locking)
 */
class MeshOptimizer {
public:
    /**
     * @brief Optimize a smooth mesh in-place
     *
     * @param buffer The mesh buffer to optimize
     * @param config Optimization settings
     */
    static void optimize(SmoothMeshBuffer& buffer, const MeshOptConfig& config = {});
    
    /**
     * @brief Generate a simplified LOD mesh
     *
     * @param input Source high-detail mesh
     * @param output Destination for simplified mesh
     * @param targetRatio Target vertex count as ratio of original (0.0-1.0)
     * @param errorThreshold Maximum error allowed
     * @param lockBorders Keep chunk border vertices fixed
     * @return Actual error achieved
     */
    static float simplify(const SmoothMeshBuffer& input,
                          SmoothMeshBuffer& output,
                          float targetRatio,
                          float errorThreshold,
                          bool lockBorders = true);
    
    /**
     * @brief Optimize vertex cache locality
     *
     * Reorders indices to improve GPU vertex cache hit rate.
     */
    static void optimizeVertexCache(SmoothMeshBuffer& buffer);
    
    /**
     * @brief Optimize vertex fetch
     *
     * Reorders vertices to improve memory access patterns.
     */
    static void optimizeVertexFetch(SmoothMeshBuffer& buffer);
    
    /**
     * @brief Get mesh statistics
     */
    struct Stats {
        size_t originalVertices = 0;
        size_t optimizedVertices = 0;
        size_t originalIndices = 0;
        size_t optimizedIndices = 0;
        float cacheHitRatio = 0.0f;      ///< After optimization
        float overdrawRatio = 0.0f;       ///< After optimization
    };
    
    static Stats getStats(const SmoothMeshBuffer& buffer);
};

} // namespace voxel
} // namespace jupiter



