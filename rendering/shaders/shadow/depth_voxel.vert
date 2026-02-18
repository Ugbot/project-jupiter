#version 460 core

/**
 * Shadow map depth pass - voxel vertex shader
 *
 * Transforms voxel vertices to light space for shadow map generation.
 * Takes 8-byte VoxelVertexGPU format from stb_voxel_render Mode 30.
 *
 * Mode 30 uses STBVOX_ICONFIG_VERTEX_32_XYZA encoding:
 *   attr_vertex (uint32): X[0:7] Y[8:15] Z[16:23] AO[24:31]
 *   attr_face (uint32): unused for shadow pass (only position needed)
 */

// Vertex input (matches VoxelVertexGPU struct - 8 bytes)
layout(location = 0) in uint inAttrVertex;  // Packed position + AO
layout(location = 1) in uint inAttrFace;    // Packed face data (unused for shadows)

// Shadow UBO - same as standard depth.vert
layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 lightSpaceMatrix;
    vec4 lightPosition;
    float shadowMinBias;
    float shadowMaxBias;
    float shadowNearPlane;
    float shadowFarPlane;
} shadowUBO;

// Voxel chunk push constants
layout(push_constant) uniform VoxelChunkPushConstant {
    vec4 chunkOffset;  // xyz = world offset for this chunk, w = unused
    vec4 scale;        // xyz = scale from stb transform, w = unused
} chunk;

void main() {
    // Unpack position from attr_vertex (Mode 30 XYZA format)
    // stbvox_vertex_encode(x,y,z,ao,texlerp) = ((x)+((y)<<8)+((z)<<16)+((ao)<<24))
    // bits 0-7:   X position (0-255)
    // bits 8-15:  Y position (0-255)
    // bits 16-23: Z position (0-255)
    float localX = float(inAttrVertex & 0xFFu);
    float localY = float((inAttrVertex >> 8u) & 0xFFu);
    float localZ = float((inAttrVertex >> 16u) & 0xFFu);

    // Scale local position by stb transform scale
    vec3 localPos = vec3(localX, localY, localZ) * chunk.scale.xyz;

    // World position = chunk offset + scaled local position
    vec3 worldPos = localPos + chunk.chunkOffset.xyz;

    // Transform vertex to light space for shadow map
    gl_Position = shadowUBO.lightSpaceMatrix * vec4(worldPos, 1.0);
}
