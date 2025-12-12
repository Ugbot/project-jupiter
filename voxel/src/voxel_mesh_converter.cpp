/**
 * @file voxel_mesh_converter.cpp
 * @brief Implementation of stb_voxel_render to Vertex3DLit conversion
 */

#include "voxel/voxel_mesh_converter.h"
#include <cstring>
#include <algorithm>

namespace jupiter {
namespace voxel {

// Static member definitions
constexpr glm::vec3 VoxelMeshConverter::FACE_NORMALS[6];
constexpr glm::vec3 VoxelMeshConverter::FACE_TANGENTS[6];

glm::vec3 VoxelMeshConverter::unpackPosition(uint32_t attr_vertex, const glm::vec3& scale) {
    // Extract position bits from attr_vertex
    // bits 0-6: X (0-127)
    // bits 7-13: Y (0-127)
    // bits 14-22: Z (0-511)
    float x = static_cast<float>(attr_vertex & 0x7F);
    float y = static_cast<float>((attr_vertex >> 7) & 0x7F);
    float z = static_cast<float>((attr_vertex >> 14) & 0x1FF);

    return glm::vec3(x, y, z) * scale;
}

float VoxelMeshConverter::unpackAO(uint32_t attr_vertex) {
    // bits 23-28: AO (0-63)
    uint32_t ao = (attr_vertex >> 23) & 0x3F;
    return static_cast<float>(ao) / 63.0f;
}

uint32_t VoxelMeshConverter::unpackNormalIndex(uint32_t attr_face) {
    // bits 0-4: normal index (0-31, only 0-5 used for cube faces)
    // Note: The exact bit layout depends on stb_voxel_render mode
    // In Mode 30, normal is typically in lower bits after color
    uint32_t normalIdx = (attr_face >> 5) & 0x1F;
    return std::min(normalIdx, 5u);
}

uint32_t VoxelMeshConverter::unpackColorIndex(uint32_t attr_face) {
    // bits 0-5: color/material index (0-63)
    return attr_face & 0x3F;
}

glm::vec2 VoxelMeshConverter::generateTexCoords(const glm::vec3& localPos, uint32_t normalIndex) {
    // Generate UVs by projecting position onto the face plane
    // The UV coordinates tile every 16 units (chunk size)
    constexpr float UV_SCALE = 1.0f / 16.0f;

    glm::vec2 uv;
    switch (normalIndex) {
        case 0: // +X face
        case 2: // -X face
            uv.x = localPos.y * UV_SCALE;
            uv.y = localPos.z * UV_SCALE;
            break;
        case 1: // +Y face
        case 3: // -Y face
            uv.x = localPos.x * UV_SCALE;
            uv.y = localPos.z * UV_SCALE;
            break;
        case 4: // +Z face
        case 5: // -Z face
            uv.x = localPos.x * UV_SCALE;
            uv.y = localPos.y * UV_SCALE;
            break;
        default:
            uv = glm::vec2(0.0f);
            break;
    }

    return uv;
}

rendering::Vertex3DLit VoxelMeshConverter::convertSingleVertex(
    const StbVoxelVertex& src,
    const glm::vec3& chunkWorldOffset,
    const glm::vec3& scale)
{
    rendering::Vertex3DLit dst;

    // Unpack and transform position to world space
    glm::vec3 localPos = unpackPosition(src.attr_vertex, scale);
    glm::vec3 worldPos = chunkWorldOffset + localPos;

    dst.pos[0] = worldPos.x;
    dst.pos[1] = worldPos.y;
    dst.pos[2] = worldPos.z;

    // Get normal from face index
    uint32_t normalIdx = unpackNormalIndex(src.attr_face);
    glm::vec3 normal = FACE_NORMALS[normalIdx];

    dst.normal[0] = normal.x;
    dst.normal[1] = normal.y;
    dst.normal[2] = normal.z;

    // Generate texture coordinates
    glm::vec2 uv = generateTexCoords(localPos, normalIdx);
    dst.texCoord[0] = uv.x;
    dst.texCoord[1] = uv.y;

    // Get tangent for this face
    glm::vec3 tangent = FACE_TANGENTS[normalIdx];
    dst.tangent[0] = tangent.x;
    dst.tangent[1] = tangent.y;
    dst.tangent[2] = tangent.z;
    dst.tangent[3] = 1.0f;  // Positive handedness

    return dst;
}

void VoxelMeshConverter::convertToLit(
    const StbVoxelVertex* src,
    uint32_t vertexCount,
    rendering::Vertex3DLit* dst,
    const glm::vec3& chunkWorldOffset,
    const glm::vec3& scale)
{
    for (uint32_t i = 0; i < vertexCount; ++i) {
        dst[i] = convertSingleVertex(src[i], chunkWorldOffset, scale);
    }
}

void VoxelMeshConverter::generateQuadIndices(uint32_t numQuads, uint32_t* outIndices) {
    // Each quad is 4 vertices, generating 2 triangles (6 indices)
    // Quad vertices: v0, v1, v2, v3
    // Triangles: (v0, v1, v2), (v0, v2, v3)
    for (uint32_t q = 0; q < numQuads; ++q) {
        uint32_t base = q * 4;
        uint32_t idx = q * 6;

        outIndices[idx + 0] = base + 0;
        outIndices[idx + 1] = base + 1;
        outIndices[idx + 2] = base + 2;
        outIndices[idx + 3] = base + 0;
        outIndices[idx + 4] = base + 2;
        outIndices[idx + 5] = base + 3;
    }
}

void VoxelMeshConverter::generateQuadIndices(uint32_t numQuads, std::vector<uint32_t>& outIndices) {
    outIndices.resize(numQuads * 6);
    generateQuadIndices(numQuads, outIndices.data());
}

} // namespace voxel
} // namespace jupiter
