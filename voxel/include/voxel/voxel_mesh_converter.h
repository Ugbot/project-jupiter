#pragma once

/**
 * @file voxel_mesh_converter.h
 * @brief Converts stb_voxel_render 8-byte vertices to Vertex3DLit format
 *
 * Used for hybrid LOD system where nearby chunks use full PBR rendering
 * with Vertex3DLit (48 bytes) and distant chunks use compact VoxelVertexGPU (8 bytes).
 *
 * Following Project Jupiter principles:
 * - No runtime allocations (caller provides output buffer)
 * - SIMD-friendly data layout
 * - Cache-efficient batch processing
 */

#include "voxel_mesher.h"
#include "rendering/vertex_formats.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

/**
 * @brief Converts stb_voxel_render vertices to standard Vertex3DLit format
 *
 * Thread-safety: Stateless, can be used from any thread.
 */
class VoxelMeshConverter {
public:
    /**
     * @brief Face normals for cube faces (6 directions)
     *
     * Index mapping from stb_voxel_render:
     *   0 = +X, 1 = +Y, 2 = -X, 3 = -Y, 4 = +Z (up), 5 = -Z (down)
     */
    static constexpr glm::vec3 FACE_NORMALS[6] = {
        glm::vec3( 1.0f,  0.0f,  0.0f),  // 0: +X
        glm::vec3( 0.0f,  1.0f,  0.0f),  // 1: +Y
        glm::vec3(-1.0f,  0.0f,  0.0f),  // 2: -X
        glm::vec3( 0.0f, -1.0f,  0.0f),  // 3: -Y
        glm::vec3( 0.0f,  0.0f,  1.0f),  // 4: +Z (up)
        glm::vec3( 0.0f,  0.0f, -1.0f)   // 5: -Z (down)
    };

    /**
     * @brief Tangent vectors for each face (for PBR normal mapping)
     *
     * Tangent is perpendicular to normal, in the "U" direction of UVs.
     */
    static constexpr glm::vec3 FACE_TANGENTS[6] = {
        glm::vec3( 0.0f,  0.0f, -1.0f),  // 0: +X face -> tangent = -Z
        glm::vec3( 1.0f,  0.0f,  0.0f),  // 1: +Y face -> tangent = +X
        glm::vec3( 0.0f,  0.0f,  1.0f),  // 2: -X face -> tangent = +Z
        glm::vec3(-1.0f,  0.0f,  0.0f),  // 3: -Y face -> tangent = -X
        glm::vec3( 1.0f,  0.0f,  0.0f),  // 4: +Z face -> tangent = +X
        glm::vec3( 1.0f,  0.0f,  0.0f)   // 5: -Z face -> tangent = +X
    };

    /**
     * @brief Convert a batch of stb vertices to Vertex3DLit format
     *
     * @param src Input stb_voxel_render vertices (8 bytes each)
     * @param vertexCount Number of vertices to convert
     * @param dst Output Vertex3DLit array (must have space for vertexCount)
     * @param chunkWorldOffset World position of chunk origin
     * @param scale Scale factor from stb_voxel_render (typically 1/127 * CHUNK_SIZE)
     */
    static void convertToLit(
        const StbVoxelVertex* src,
        uint32_t vertexCount,
        rendering::Vertex3DLit* dst,
        const glm::vec3& chunkWorldOffset,
        const glm::vec3& scale);

    /**
     * @brief Convert a single stb vertex to Vertex3DLit
     *
     * @param src Input stb_voxel_render vertex
     * @param chunkWorldOffset World position of chunk origin
     * @param scale Scale factor from stb_voxel_render
     * @return Converted Vertex3DLit
     */
    static rendering::Vertex3DLit convertSingleVertex(
        const StbVoxelVertex& src,
        const glm::vec3& chunkWorldOffset,
        const glm::vec3& scale);

    /**
     * @brief Generate triangle indices from quad vertices
     *
     * stb_voxel_render outputs quads (4 vertices each).
     * This generates 6 indices per quad for triangle rendering.
     *
     * @param numQuads Number of quads
     * @param outIndices Output index buffer (must have space for numQuads * 6)
     */
    static void generateQuadIndices(uint32_t numQuads, uint32_t* outIndices);

    /**
     * @brief Generate triangle indices into a vector (convenience)
     */
    static void generateQuadIndices(uint32_t numQuads, std::vector<uint32_t>& outIndices);

    /**
     * @brief Unpack position from attr_vertex
     *
     * @param attr_vertex Packed vertex attribute
     * @param scale Scale factor
     * @return Local position (0-CHUNK_SIZE range)
     */
    static glm::vec3 unpackPosition(uint32_t attr_vertex, const glm::vec3& scale);

    /**
     * @brief Unpack ambient occlusion from attr_vertex
     *
     * @param attr_vertex Packed vertex attribute
     * @return AO value (0.0 - 1.0)
     */
    static float unpackAO(uint32_t attr_vertex);

    /**
     * @brief Unpack normal index from attr_face
     *
     * @param attr_face Packed face attribute
     * @return Normal index (0-5)
     */
    static uint32_t unpackNormalIndex(uint32_t attr_face);

    /**
     * @brief Unpack color/material index from attr_face
     *
     * @param attr_face Packed face attribute
     * @return Color/material index (0-63)
     */
    static uint32_t unpackColorIndex(uint32_t attr_face);

    /**
     * @brief Generate texture coordinates based on face normal
     *
     * Projects local position onto the face plane to generate UVs.
     *
     * @param localPos Local position in voxel units
     * @param normalIndex Face normal index (0-5)
     * @return UV coordinates (0-1 range per voxel)
     */
    static glm::vec2 generateTexCoords(const glm::vec3& localPos, uint32_t normalIndex);
};

} // namespace voxel
} // namespace jupiter
