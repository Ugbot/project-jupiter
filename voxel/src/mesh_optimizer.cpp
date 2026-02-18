/**
 * @file mesh_optimizer.cpp
 * @brief Implementation of mesh optimization using meshoptimizer
 */

#include <voxel/mesh_optimizer.h>
#include <meshoptimizer.h>
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace voxel {

// Thread-local buffers to avoid allocations
ThreadLocalMeshBuffers& getThreadLocalMeshBuffers() {
    static thread_local ThreadLocalMeshBuffers tls_buffers;
    return tls_buffers;
}

void MeshOptimizer::optimize(SmoothMeshBuffer& buffer, const MeshOptConfig& config) {
    if (buffer.vertices.empty() || buffer.indices.empty()) {
        return;
    }
    
    // Vertex cache optimization
    if (config.optimizeVertexCache) {
        optimizeVertexCache(buffer);
    }
    
    // Vertex fetch optimization
    if (config.optimizeVertexFetch) {
        optimizeVertexFetch(buffer);
    }
}

void MeshOptimizer::optimizeVertexCache(SmoothMeshBuffer& buffer) {
    if (buffer.indices.size() < 3) return;
    
    // meshopt_optimizeVertexCache reorders indices in-place for better cache utilization
    meshopt_optimizeVertexCache(
        buffer.indices.data(),
        buffer.indices.data(),
        buffer.indices.size(),
        buffer.vertices.size()
    );
}

void MeshOptimizer::optimizeVertexFetch(SmoothMeshBuffer& buffer) {
    if (buffer.vertices.empty() || buffer.indices.empty()) return;
    
    ThreadLocalMeshBuffers& tls = getThreadLocalMeshBuffers();
    tls.remapBuffer.resize(buffer.vertices.size());
    
    // Generate remap table
    size_t uniqueVertexCount = meshopt_optimizeVertexFetchRemap(
        tls.remapBuffer.data(),
        buffer.indices.data(),
        buffer.indices.size(),
        buffer.vertices.size()
    );
    
    // Apply remap to indices
    meshopt_remapIndexBuffer(
        buffer.indices.data(),
        buffer.indices.data(),
        buffer.indices.size(),
        tls.remapBuffer.data()
    );
    
    // Apply remap to vertices
    tls.vertices.resize(uniqueVertexCount);
    meshopt_remapVertexBuffer(
        tls.vertices.data(),
        buffer.vertices.data(),
        buffer.vertices.size(),
        sizeof(SmoothVertex),
        tls.remapBuffer.data()
    );
    
    // Copy back optimized vertices
    buffer.vertices.resize(uniqueVertexCount);
    std::memcpy(buffer.vertices.data(), tls.vertices.data(), 
                uniqueVertexCount * sizeof(SmoothVertex));
}

float MeshOptimizer::simplify(const SmoothMeshBuffer& input,
                               SmoothMeshBuffer& output,
                               float targetRatio,
                               float errorThreshold,
                               bool lockBorders) {
    if (input.vertices.empty() || input.indices.empty()) {
        output = input;
        return 0.0f;
    }
    
    // Calculate target index count
    size_t targetIndexCount = static_cast<size_t>(
        static_cast<float>(input.indices.size()) * targetRatio
    );
    // Round to multiple of 3 (triangles)
    targetIndexCount = (targetIndexCount / 3) * 3;
    if (targetIndexCount < 3) targetIndexCount = 3;
    
    // Prepare output
    output.vertices = input.vertices;
    output.indices.resize(input.indices.size());
    
    float resultError = 0.0f;
    
    // Simplify using meshoptimizer
    // Extract positions for simplification (meshopt needs float[3] array)
    ThreadLocalMeshBuffers& tls = getThreadLocalMeshBuffers();
    
    size_t resultIndexCount = meshopt_simplify(
        output.indices.data(),
        input.indices.data(),
        input.indices.size(),
        reinterpret_cast<const float*>(input.vertices.data()), // position is first member
        input.vertices.size(),
        sizeof(SmoothVertex),
        targetIndexCount,
        errorThreshold,
        lockBorders ? meshopt_SimplifyLockBorder : 0,
        &resultError
    );
    
    output.indices.resize(resultIndexCount);
    
    // Optimize the simplified mesh
    if (resultIndexCount > 0) {
        MeshOptConfig config;
        config.generateLOD = false;
        optimize(output, config);
    }
    
    // Copy metadata
    output.chunkX = input.chunkX;
    output.chunkY = input.chunkY;
    output.chunkZ = input.chunkZ;
    output.lodLevel = input.lodLevel;
    
    return resultError;
}

MeshOptimizer::Stats MeshOptimizer::getStats(const SmoothMeshBuffer& buffer) {
    Stats stats;
    stats.originalVertices = buffer.vertices.size();
    stats.optimizedVertices = buffer.vertices.size();
    stats.originalIndices = buffer.indices.size();
    stats.optimizedIndices = buffer.indices.size();
    
    if (buffer.indices.size() >= 3 && buffer.vertices.size() > 0) {
        // Analyze vertex cache performance (simulates a 16-entry FIFO cache)
        meshopt_VertexCacheStatistics vcache = meshopt_analyzeVertexCache(
            buffer.indices.data(),
            buffer.indices.size(),
            buffer.vertices.size(),
            16,  // cache size
            0,   // warp size (0 = no warp optimization)
            0    // buffer size
        );
        
        // ACMR (Average Cache Miss Ratio) - lower is better
        // Transform to hit ratio (higher is better)
        stats.cacheHitRatio = 1.0f - vcache.acmr;
        
        // Analyze overdraw
        meshopt_OverdrawStatistics overdraw = meshopt_analyzeOverdraw(
            buffer.indices.data(),
            buffer.indices.size(),
            reinterpret_cast<const float*>(buffer.vertices.data()),
            buffer.vertices.size(),
            sizeof(SmoothVertex)
        );
        
        stats.overdrawRatio = overdraw.overdraw;
    }
    
    return stats;
}

} // namespace voxel
} // namespace jupiter



