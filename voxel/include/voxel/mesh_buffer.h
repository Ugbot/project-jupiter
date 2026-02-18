#pragma once

/**
 * @file mesh_buffer.h
 * @brief Buffer for kernel-based voxel mesh output
 *
 * Holds vertex data output from the kernel meshing pipeline.
 */

#include "vertex_encoder.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace jupiter {
namespace voxel {

/**
 * @brief Output buffer for kernel-based voxel mesh data
 *
 * Holds the vertices and quad info needed for GPU rendering.
 * Named differently from existing MeshBuffer in mesh_buffer_pool.h.
 */
struct KernelMeshBuffer {
    /// Vertex data
    std::vector<VoxelVertexGPU> vertices;
    
    /// Number of quads (for index generation)
    size_t quadCount = 0;
    
    /// Flip flags for each quad (for AO-correct triangulation)
    std::vector<bool> flipFlags;
    
    /// Chunk coordinate this mesh is for
    ChunkCoord chunkCoord;
    
    /// Generation counter (matches ChunkColumns edit generation)
    uint64_t generation = 0;
    
    // ========================================================================
    // Access
    // ========================================================================
    
    /**
     * @brief Get vertex count
     */
    size_t vertexCount() const {
        return vertices.size();
    }
    
    /**
     * @brief Get index count (for indexed rendering)
     */
    size_t indexCount() const {
        return quadCount * 6;
    }
    
    /**
     * @brief Get data pointer for GPU upload
     */
    const void* data() const {
        return vertices.data();
    }
    
    /**
     * @brief Get data size in bytes
     */
    size_t dataSize() const {
        return vertices.size() * sizeof(VoxelVertexGPU);
    }
    
    /**
     * @brief Check if mesh is empty
     */
    bool empty() const {
        return vertices.empty();
    }
    
    /**
     * @brief Clear the buffer
     */
    void clear() {
        vertices.clear();
        flipFlags.clear();
        quadCount = 0;
        generation = 0;
    }
    
    /**
     * @brief Reserve space for expected vertex count
     */
    void reserve(size_t vertexCapacity) {
        vertices.reserve(vertexCapacity);
        flipFlags.reserve(vertexCapacity / 4);
    }
    
    // ========================================================================
    // Index Generation
    // ========================================================================
    
    /**
     * @brief Generate index buffer for this mesh
     */
    std::vector<uint32_t> generateIndices() const {
        // Convert bool vector to array for generateQuadIndices
        std::vector<bool> flipCopy(flipFlags.begin(), flipFlags.end());
        
        std::vector<uint32_t> indices;
        indices.reserve(quadCount * 6);
        
        for (size_t i = 0; i < quadCount; ++i) {
            uint32_t base = static_cast<uint32_t>(i * 4);
            bool flip = i < flipCopy.size() && flipCopy[i];
            
            if (flip) {
                indices.push_back(base + 0);
                indices.push_back(base + 1);
                indices.push_back(base + 3);
                indices.push_back(base + 1);
                indices.push_back(base + 2);
                indices.push_back(base + 3);
            } else {
                indices.push_back(base + 0);
                indices.push_back(base + 1);
                indices.push_back(base + 2);
                indices.push_back(base + 0);
                indices.push_back(base + 2);
                indices.push_back(base + 3);
            }
        }
        
        return indices;
    }
};

/**
 * @brief Pool of reusable kernel mesh buffers
 *
 * Avoids allocation churn by reusing mesh buffers.
 */
class KernelMeshBufferPool {
public:
    KernelMeshBufferPool() = default;
    ~KernelMeshBufferPool() = default;
    
    /**
     * @brief Acquire a mesh buffer (may be recycled)
     */
    KernelMeshBuffer* acquire() {
        if (!freeList_.empty()) {
            KernelMeshBuffer* buf = freeList_.back();
            freeList_.pop_back();
            buf->clear();
            return buf;
        }
        
        buffers_.push_back(std::make_unique<KernelMeshBuffer>());
        return buffers_.back().get();
    }
    
    /**
     * @brief Release a mesh buffer back to the pool
     */
    void release(KernelMeshBuffer* buffer) {
        if (buffer) {
            freeList_.push_back(buffer);
        }
    }
    
    /**
     * @brief Get number of active (non-free) buffers
     */
    size_t activeCount() const {
        return buffers_.size() - freeList_.size();
    }
    
    /**
     * @brief Get total buffer count
     */
    size_t totalCount() const {
        return buffers_.size();
    }
    
private:
    std::vector<std::unique_ptr<KernelMeshBuffer>> buffers_;
    std::vector<KernelMeshBuffer*> freeList_;
};

} // namespace voxel
} // namespace jupiter

