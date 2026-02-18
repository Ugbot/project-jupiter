#pragma once

/**
 * @file smooth_vertex.h
 * @brief Vertex format for smooth terrain rendering
 *
 * Used by Marching Cubes and Transvoxel meshers.
 */

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace jupiter {
namespace voxel {

/**
 * @brief Vertex for smooth terrain meshes
 *
 * Contains position, normal, material, and AO data.
 * 32 bytes for GPU efficiency.
 */
struct SmoothVertex {
    /// World-space position
    glm::vec3 position;         // 12 bytes
    
    /// Packed normal (10-10-10-2 format would be ideal, using vec3 for clarity)
    glm::vec3 normal;           // 12 bytes
    
    /// Material/block type index
    uint8_t materialId;         // 1 byte
    
    /// Ambient occlusion (0-255)
    uint8_t ao;                 // 1 byte
    
    /// Texture coordinates / blend weights
    uint8_t texBlendU;          // 1 byte
    uint8_t texBlendV;          // 1 byte
    
    /// Secondary material for blending (triplanar)
    uint8_t secondaryMaterialId; // 1 byte
    
    /// Blend factor between primary and secondary (0-255)
    uint8_t blendFactor;        // 1 byte
    
    /// Padding for alignment
    uint8_t padding[2];         // 2 bytes
                                // Total: 32 bytes
    
    SmoothVertex() = default;
    
    SmoothVertex(const glm::vec3& pos, const glm::vec3& norm, uint8_t mat)
        : position(pos)
        , normal(norm)
        , materialId(mat)
        , ao(255)
        , texBlendU(0)
        , texBlendV(0)
        , secondaryMaterialId(mat)
        , blendFactor(0)
        , padding{0, 0}
    {}
};

static_assert(sizeof(SmoothVertex) == 32, "SmoothVertex must be 32 bytes");

/**
 * @brief Packed smooth vertex for GPU upload
 *
 * More compact format: 24 bytes
 * Position as 3 floats, normal packed into 32 bits
 */
struct PackedSmoothVertex {
    float x, y, z;              // 12 bytes: position
    uint32_t packedNormal;      // 4 bytes: 10-10-10-2 normal
    uint16_t packedMaterial;    // 2 bytes: material + blend
    uint8_t ao;                 // 1 byte: ambient occlusion
    uint8_t flags;              // 1 byte: flags
    uint16_t texU;              // 2 bytes: texture U
    uint16_t texV;              // 2 bytes: texture V
                                // Total: 24 bytes
    
    /**
     * @brief Pack a normal vector into 10-10-10-2 format
     */
    static uint32_t packNormal(const glm::vec3& n) {
        // Map from [-1, 1] to [0, 1023] for 10 bits
        int32_t x = static_cast<int32_t>((n.x * 0.5f + 0.5f) * 1023.0f);
        int32_t y = static_cast<int32_t>((n.y * 0.5f + 0.5f) * 1023.0f);
        int32_t z = static_cast<int32_t>((n.z * 0.5f + 0.5f) * 1023.0f);
        
        x = x < 0 ? 0 : (x > 1023 ? 1023 : x);
        y = y < 0 ? 0 : (y > 1023 ? 1023 : y);
        z = z < 0 ? 0 : (z > 1023 ? 1023 : z);
        
        return (x) | (y << 10) | (z << 20);
    }
    
    /**
     * @brief Unpack a 10-10-10-2 normal
     */
    static glm::vec3 unpackNormal(uint32_t packed) {
        float x = ((packed & 0x3FF) / 1023.0f) * 2.0f - 1.0f;
        float y = (((packed >> 10) & 0x3FF) / 1023.0f) * 2.0f - 1.0f;
        float z = (((packed >> 20) & 0x3FF) / 1023.0f) * 2.0f - 1.0f;
        return glm::normalize(glm::vec3(x, y, z));
    }
    
    /**
     * @brief Create from SmoothVertex
     */
    static PackedSmoothVertex pack(const SmoothVertex& v) {
        PackedSmoothVertex p;
        p.x = v.position.x;
        p.y = v.position.y;
        p.z = v.position.z;
        p.packedNormal = packNormal(v.normal);
        p.packedMaterial = v.materialId | (static_cast<uint16_t>(v.secondaryMaterialId) << 8);
        p.ao = v.ao;
        p.flags = v.blendFactor;
        p.texU = v.texBlendU * 256;  // Scale up for precision
        p.texV = v.texBlendV * 256;
        return p;
    }
};

static_assert(sizeof(PackedSmoothVertex) == 24, "PackedSmoothVertex must be 24 bytes");

/**
 * @brief Triangle from smooth meshing
 */
struct SmoothTriangle {
    SmoothVertex v[3];
};

/**
 * @brief Mesh output buffer for smooth terrain
 */
struct SmoothMeshBuffer {
    std::vector<SmoothVertex> vertices;
    std::vector<uint32_t> indices;
    
    /// Chunk coordinate
    int32_t chunkX = 0;
    int32_t chunkY = 0;
    int32_t chunkZ = 0;
    
    /// LOD level this mesh represents
    uint8_t lodLevel = 0;
    
    /// Whether this includes transition cells
    bool hasTransitions = false;
    
    /**
     * @brief Clear the buffer
     */
    void clear() {
        vertices.clear();
        indices.clear();
        hasTransitions = false;
    }
    
    /**
     * @brief Reserve space
     */
    void reserve(size_t vertexCount, size_t indexCount) {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    }
    
    /**
     * @brief Add a vertex, return its index
     */
    uint32_t addVertex(const SmoothVertex& v) {
        uint32_t idx = static_cast<uint32_t>(vertices.size());
        vertices.push_back(v);
        return idx;
    }
    
    /**
     * @brief Add a triangle by indices
     */
    void addTriangle(uint32_t i0, uint32_t i1, uint32_t i2) {
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
    }
    
    /**
     * @brief Add a triangle with vertices
     */
    void addTriangle(const SmoothVertex& v0, const SmoothVertex& v1, const SmoothVertex& v2) {
        uint32_t i0 = addVertex(v0);
        uint32_t i1 = addVertex(v1);
        uint32_t i2 = addVertex(v2);
        addTriangle(i0, i1, i2);
    }
    
    /**
     * @brief Get triangle count
     */
    size_t triangleCount() const {
        return indices.size() / 3;
    }
    
    /**
     * @brief Check if empty
     */
    bool empty() const {
        return vertices.empty();
    }
};

} // namespace voxel
} // namespace jupiter



