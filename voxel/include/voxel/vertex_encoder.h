#pragma once

/**
 * @file vertex_encoder.h
 * @brief Encodes voxel quads into GPU vertex format
 *
 * Packs merged quads into the compact 8-byte VoxelVertexGPU format
 * for efficient GPU rendering.
 */

#include "voxel_types.h"
#include "greedy_mesher.h"
#include "face_culler.h"
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

// ============================================================================
// VoxelVertexGPU - 8-byte GPU Vertex Format
// ============================================================================

/**
 * @brief GPU vertex format for voxel rendering
 *
 * Packed 8-byte format matching Mode 30 layout:
 * - attr_vertex (uint32): X[0:7] Y[8:15] Z[16:23] AO[24:31]
 * - attr_face (uint32): tex1[0:7] tex2[8:15] color[16:23] face_info[24:31]
 *   where face_info = (normal << 2) + facerot
 */
struct VoxelVertexGPU {
    uint32_t attrVertex;  ///< Position + AO
    uint32_t attrFace;    ///< Face data (normal, color, texture)
    
    /**
     * @brief Encode position and AO into attrVertex
     */
    static uint32_t encodeVertex(uint8_t x, uint8_t y, uint8_t z, uint8_t ao) {
        return static_cast<uint32_t>(x) |
               (static_cast<uint32_t>(y) << 8) |
               (static_cast<uint32_t>(z) << 16) |
               (static_cast<uint32_t>(ao) << 24);
    }
    
    /**
     * @brief Encode face data into attrFace
     */
    static uint32_t encodeFace(uint8_t tex1, uint8_t tex2, uint8_t color,
                               uint8_t normal, uint8_t faceRot = 0) {
        uint8_t faceInfo = (normal << 2) | (faceRot & 0x3);
        return static_cast<uint32_t>(tex1) |
               (static_cast<uint32_t>(tex2) << 8) |
               (static_cast<uint32_t>(color) << 16) |
               (static_cast<uint32_t>(faceInfo) << 24);
    }
};

static_assert(sizeof(VoxelVertexGPU) == 8, "VoxelVertexGPU must be 8 bytes");

// ============================================================================
// VertexEncoder
// ============================================================================

/**
 * @brief Encodes merged quads into GPU vertex buffer
 *
 * Converts MergedQuad output from GreedyMesher into VoxelVertexGPU
 * arrays suitable for GPU rendering.
 *
 * Each quad becomes 4 vertices (for indexed rendering) or 6 vertices
 * (for non-indexed rendering with triangles).
 */
class VertexEncoder {
public:
    VertexEncoder() = default;
    ~VertexEncoder() = default;
    
    // ========================================================================
    // Encoding
    // ========================================================================
    
    /**
     * @brief Encode merged quads to GPU vertices (indexed)
     *
     * Outputs 4 vertices per quad. Caller should use index buffer:
     * For each quad: 0,1,2, 2,3,0 (or 0,1,3, 1,2,3 if quad.ao.shouldFlip())
     *
     * @param quads Merged quads from GreedyMesher
     * @param chunkOffset Chunk world offset (chunkCoord * CHUNK_SIZE)
     */
    void encode(const std::vector<MergedQuad>& quads,
                const ChunkCoord& chunkCoord);
    
    /**
     * @brief Encode to non-indexed triangles (6 vertices per quad)
     */
    void encodeTriangles(const std::vector<MergedQuad>& quads,
                         const ChunkCoord& chunkCoord);
    
    // ========================================================================
    // Results
    // ========================================================================
    
    /**
     * @brief Get encoded vertices
     */
    const std::vector<VoxelVertexGPU>& vertices() const {
        return vertices_;
    }
    
    std::vector<VoxelVertexGPU>& vertices() {
        return vertices_;
    }
    
    /**
     * @brief Get vertex count
     */
    size_t vertexCount() const {
        return vertices_.size();
    }
    
    /**
     * @brief Get quad count (for index generation)
     */
    size_t quadCount() const {
        return quadCount_;
    }
    
    /**
     * @brief Clear output
     */
    void clear() {
        vertices_.clear();
        quadCount_ = 0;
    }
    
    /**
     * @brief Get data pointer for GPU upload
     */
    const void* data() const {
        return vertices_.data();
    }
    
    /**
     * @brief Get data size in bytes
     */
    size_t dataSize() const {
        return vertices_.size() * sizeof(VoxelVertexGPU);
    }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set block-to-texture mapping function
     *
     * @param fn Function: (BlockType block, FaceDirection face) -> uint8_t tex1
     */
    using TextureMapper = uint8_t(*)(BlockType block, uint8_t face);
    
    void setTextureMapper(TextureMapper mapper) {
        textureMapper_ = mapper;
    }
    
    /**
     * @brief Set block-to-color mapping function
     */
    using ColorMapper = uint8_t(*)(BlockType block);
    
    void setColorMapper(ColorMapper mapper) {
        colorMapper_ = mapper;
    }
    
private:
    /**
     * @brief Encode a single quad to 4 vertices
     */
    void encodeQuad(const MergedQuad& quad, const ChunkCoord& chunkCoord);
    
    /**
     * @brief Get vertex positions for a face
     */
    void getQuadVertices(const MergedQuad& quad,
                         uint8_t outX[4],
                         uint8_t outY[4],
                         uint8_t outZ[4]) const;
    
    /**
     * @brief Default texture mapper (block type = texture index)
     */
    static uint8_t defaultTextureMapper(BlockType block, uint8_t face) {
        (void)face;
        return static_cast<uint8_t>(block);
    }
    
    /**
     * @brief Default color mapper (white)
     */
    static uint8_t defaultColorMapper(BlockType block) {
        (void)block;
        return 255;  // Full brightness/white
    }
    
    /// Output vertices
    std::vector<VoxelVertexGPU> vertices_;
    
    /// Number of quads encoded
    size_t quadCount_ = 0;
    
    /// Texture mapping function
    TextureMapper textureMapper_ = defaultTextureMapper;
    
    /// Color mapping function
    ColorMapper colorMapper_ = defaultColorMapper;
};

// ============================================================================
// Index Buffer Generation
// ============================================================================

/**
 * @brief Generate index buffer for quad rendering
 *
 * Creates indices for the given number of quads, with optional
 * flip info for proper AO triangulation.
 *
 * @param quadCount Number of quads
 * @param flipFlags Optional: one bool per quad indicating if it should flip
 * @return Vector of indices (6 per quad)
 */
std::vector<uint32_t> generateQuadIndices(size_t quadCount,
                                          const bool* flipFlags = nullptr);

/**
 * @brief Generate index buffer for specific quad AO values
 */
std::vector<uint32_t> generateQuadIndices(const std::vector<QuadAO>& aoValues);

} // namespace voxel
} // namespace jupiter



