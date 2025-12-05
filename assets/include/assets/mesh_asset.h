#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace jupiter {
namespace assets {

/**
 * @brief Vertex format identifier
 *
 * Identifies the layout of vertex data in a mesh.
 * Used by rendering module to configure vertex input descriptions.
 */
enum class VertexFormat : uint32_t {
    VERTEX_2D = 0,          // pos[2] + color[3] = 20 bytes
    VERTEX_3D = 1,          // pos[3] + color[3] = 24 bytes
    VERTEX_3D_LIT = 2,      // pos[3] + normal[3] + texCoord[2] + tangent[4] = 48 bytes
    VERTEX_3D_SKINNED = 3,  // pos[3] + normal[3] + texCoord[2] + tangent[4] + joints[4] + weights[4] = 68 bytes
};

/**
 * @brief Raw mesh data (CPU-only, no GPU buffers)
 *
 * Contains vertex and index data as raw float/uint32 arrays.
 * No Vulkan types - can be loaded without GPU context.
 *
 * Following CLAUDE.md:
 * - POD layout where possible
 * - Pre-allocated during load (no runtime allocations)
 * - Cache-friendly data layout
 *
 * Memory is allocated once during load and never reallocated.
 */
struct MeshAsset {
    std::string uuid;       // Unique identifier
    std::string name;       // Human-readable name

    // Vertex data (raw floats, format-dependent layout)
    std::vector<float> vertexData;  // Pre-allocated during load
    uint32_t vertexCount;           // Number of vertices
    uint32_t vertexStride;          // Size of one vertex in bytes
    VertexFormat vertexFormat;      // Layout of vertex data

    // Index data (raw uint32)
    std::vector<uint32_t> indexData;  // Pre-allocated during load
    uint32_t indexCount;              // Number of indices

    // Material reference (UUID, not pointer - CPU/GPU independent)
    std::string materialUuid;

    // AABB for culling (optional, computed during load)
    float aabbMin[3];  // Minimum corner
    float aabbMax[3];  // Maximum corner

    // Skeletal animation data (optional, only for VERTEX_3D_SKINNED)
    // Stored separately to allow easy bone index remapping for LOD
    std::vector<uint8_t> jointIndices;   // 4 joints per vertex (packed)
    std::vector<float> jointWeights;     // 4 weights per vertex

    /**
     * @brief Default constructor
     */
    MeshAsset()
        : vertexCount(0)
        , vertexStride(0)
        , vertexFormat(VertexFormat::VERTEX_3D_LIT)
        , indexCount(0)
        , aabbMin{0.0f, 0.0f, 0.0f}
        , aabbMax{0.0f, 0.0f, 0.0f} {}

    /**
     * @brief Check if this mesh has skeletal animation data
     */
    bool isSkinned() const {
        return vertexFormat == VertexFormat::VERTEX_3D_SKINNED && !jointIndices.empty();
    }

    /**
     * @brief Remap joint indices for skeleton LOD
     *
     * Remaps all vertex joint indices using the provided remap table.
     * Used when switching to a lower LOD skeleton with fewer bones.
     *
     * @param remapTable Maps original bone index to LOD bone index (-1 = removed)
     * @param fallbackBone Bone to use if original is removed (typically root = 0)
     */
    void remapJointsForLOD(const int32_t* remapTable, uint8_t fallbackBone) {
        if (!isSkinned()) return;

        for (size_t i = 0; i < jointIndices.size(); i++) {
            int32_t remapped = remapTable[jointIndices[i]];
            if (remapped >= 0) {
                jointIndices[i] = static_cast<uint8_t>(remapped);
            } else {
                jointIndices[i] = fallbackBone;
            }
        }
    }

    /**
     * @brief Get vertex stride for a given format
     *
     * Note: VERTEX_3D_SKINNED returns the base stride without joint/weight data.
     * Joint indices (4 bytes) and weights (16 bytes) are stored separately
     * to allow easy bone remapping for LOD systems.
     */
    static uint32_t getVertexStride(VertexFormat format) {
        switch (format) {
            case VertexFormat::VERTEX_2D:      return 20;  // 2*4 + 3*4
            case VertexFormat::VERTEX_3D:      return 24;  // 3*4 + 3*4
            case VertexFormat::VERTEX_3D_LIT:  return 48;  // 3*4 + 3*4 + 2*4 + 4*4
            case VertexFormat::VERTEX_3D_SKINNED: return 48;  // Same as LIT (skinning data stored separately)
            default: return 0;
        }
    }

    /**
     * @brief Get total vertex size including skinning data
     */
    static uint32_t getTotalVertexSize(VertexFormat format) {
        switch (format) {
            case VertexFormat::VERTEX_2D:      return 20;
            case VertexFormat::VERTEX_3D:      return 24;
            case VertexFormat::VERTEX_3D_LIT:  return 48;
            case VertexFormat::VERTEX_3D_SKINNED: return 68;  // 48 + 4 (joints) + 16 (weights)
            default: return 0;
        }
    }
};

} // namespace assets
} // namespace jupiter
