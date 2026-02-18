#version 460 core

/**
 * Clustered Forward PBR Vertex Shader - Voxel variant
 *
 * Takes 8-byte VoxelVertexGPU format from stb_voxel_render Mode 30.
 * Outputs same interface as pbr_clustered.vert for compatibility with
 * pbr_clustered.frag.
 *
 * Input format (VoxelVertexGPU):
 *   attr_vertex (uint32): bits 0-6 X, bits 7-13 Y, bits 14-22 Z, bits 23-28 AO
 *   attr_face (uint32): bits 0-4 normal index, bits 5+ color/material
 */

// ============================================================================
// Inputs (VoxelVertexGPU format - 8 bytes)
// ============================================================================

layout(location = 0) in uint inAttrVertex;  // Packed position + AO + texlerp
layout(location = 1) in uint inAttrFace;    // Packed face data (normal, color, tex)

// ============================================================================
// Outputs (MUST match pbr_clustered.vert for pbr_clustered.frag compatibility)
// ============================================================================

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outTangent;

// ============================================================================
// Uniforms
// ============================================================================

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

// Voxel chunk push constants
layout(push_constant) uniform VoxelChunkPushConstant {
    vec4 chunkOffset;  // xyz = world offset for this chunk, w = unused
    vec4 scale;        // xyz = scale from stb transform, w = unused
} chunk;

// ============================================================================
// Lookup Tables
// ============================================================================

// Face normals (6 axis-aligned directions)
// Matches FaceDirection enum: FACE_POS_X=0, FACE_NEG_X=1, FACE_POS_Y=2, FACE_NEG_Y=3, FACE_POS_Z=4, FACE_NEG_Z=5
const vec3 FACE_NORMALS[6] = vec3[6](
    vec3( 1.0,  0.0,  0.0),   // 0: +X
    vec3(-1.0,  0.0,  0.0),   // 1: -X
    vec3( 0.0,  1.0,  0.0),   // 2: +Y
    vec3( 0.0, -1.0,  0.0),   // 3: -Y
    vec3( 0.0,  0.0,  1.0),   // 4: +Z
    vec3( 0.0,  0.0, -1.0)    // 5: -Z
);

// Face tangents (for normal mapping)
// Matches FaceDirection enum: FACE_POS_X=0, FACE_NEG_X=1, FACE_POS_Y=2, FACE_NEG_Y=3, FACE_POS_Z=4, FACE_NEG_Z=5
const vec3 FACE_TANGENTS[6] = vec3[6](
    vec3( 0.0,  0.0, -1.0),   // 0: +X face -> tangent = -Z
    vec3( 0.0,  0.0,  1.0),   // 1: -X face -> tangent = +Z
    vec3( 1.0,  0.0,  0.0),   // 2: +Y face -> tangent = +X
    vec3(-1.0,  0.0,  0.0),   // 3: -Y face -> tangent = -X
    vec3( 1.0,  0.0,  0.0),   // 4: +Z face -> tangent = +X
    vec3( 1.0,  0.0,  0.0)    // 5: -Z face -> tangent = +X
);

// ============================================================================
// Main
// ============================================================================

void main() {
    // Unpack position from attr_vertex
    // Mode 30: bits 0-6 X (0-127), bits 7-13 Y (0-127), bits 14-22 Z (0-511)
    float localX = float(inAttrVertex & 0x7Fu);
    float localY = float((inAttrVertex >> 7u) & 0x7Fu);
    float localZ = float((inAttrVertex >> 14u) & 0x1FFu);

    // Scale local position by stb transform scale
    vec3 localPos = vec3(localX, localY, localZ) * chunk.scale.xyz;

    // World position = chunk offset + scaled local position
    outWorldPos = localPos + chunk.chunkOffset.xyz;

    // Unpack normal index from attr_face (bits 0-4)
    uint normalIdx = inAttrFace & 0x1Fu;
    if (normalIdx > 5u) normalIdx = 0u;  // Clamp to valid range

    // World-space normal (voxel chunks have no rotation)
    outNormal = FACE_NORMALS[normalIdx];

    // World-space tangent with positive handedness
    outTangent = vec4(FACE_TANGENTS[normalIdx], 1.0);

    // Generate texture coordinates based on face normal
    // UV tiles every 1.0 world units
    const float UV_SCALE = 1.0;
    if (abs(outNormal.x) > 0.5) {
        // X-facing faces: project YZ
        outTexCoord = localPos.yz * UV_SCALE;
    } else if (abs(outNormal.y) > 0.5) {
        // Y-facing faces: project XZ
        outTexCoord = localPos.xz * UV_SCALE;
    } else {
        // Z-facing faces: project XY
        outTexCoord = localPos.xy * UV_SCALE;
    }

    // Final clip space position
    gl_Position = camera.viewProjection * vec4(outWorldPos, 1.0);
}
