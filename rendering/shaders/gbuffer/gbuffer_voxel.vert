#version 460 core

/**
 * G-buffer geometry pass - voxel vertex shader
 *
 * Takes 8-byte VoxelVertexGPU format from stb_voxel_render Mode 30.
 * Outputs same interface as gbuffer.vert for compatibility with gbuffer.frag.
 *
 * Input format (VoxelVertexGPU):
 *   attr_vertex (uint32): bits 0-6 X, bits 7-13 Y, bits 14-22 Z, bits 23-28 AO
 *   attr_face (uint32): bits 0-4 normal index, bits 5+ color/material
 */

// Vertex input (matches VoxelVertexGPU struct - 8 bytes)
layout(location = 0) in uint inAttrVertex;  // Packed position + AO + texlerp
layout(location = 1) in uint inAttrFace;    // Packed face data (normal, color, tex)

// Outputs to fragment shader (MUST match gbuffer.vert outputs for gbuffer.frag)
layout(location = 0) out vec3 outViewPos;
layout(location = 1) out vec3 outViewNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outViewTangent;
layout(location = 4) out vec3 outWorldPos;

// Camera UBO (Set 0, Binding 0)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;  // x=near, y=far, z=fov, w=aspect
} camera;

// Voxel chunk push constants
// NOTE: Different from standard gbuffer.vert which uses model + normalMatrix
// For voxel chunks, we use chunkOffset + scale instead (no rotation)
layout(push_constant) uniform VoxelChunkPushConstant {
    vec4 chunkOffset;  // xyz = world offset for this chunk, w = unused
    vec4 scale;        // xyz = scale from stb transform, w = unused
} chunk;

// Face normals lookup table (6 axis-aligned directions)
// stb_voxel_render normal indices: 0=east(+x), 1=north(+y), 2=west(-x), 3=south(-y), 4=up(+z), 5=down(-z)
const vec3 FACE_NORMALS[6] = vec3[6](
    vec3( 1.0,  0.0,  0.0),   // 0: +X (East)
    vec3( 0.0,  1.0,  0.0),   // 1: +Y (North)
    vec3(-1.0,  0.0,  0.0),   // 2: -X (West)
    vec3( 0.0, -1.0,  0.0),   // 3: -Y (South)
    vec3( 0.0,  0.0,  1.0),   // 4: +Z (Up)
    vec3( 0.0,  0.0, -1.0)    // 5: -Z (Down)
);

// Face tangents lookup table (for normal mapping in PBR)
// Tangent points in the UV "U" direction for each face
const vec3 FACE_TANGENTS[6] = vec3[6](
    vec3( 0.0,  0.0, -1.0),   // 0: +X face -> tangent = -Z
    vec3( 1.0,  0.0,  0.0),   // 1: +Y face -> tangent = +X
    vec3( 0.0,  0.0,  1.0),   // 2: -X face -> tangent = +Z
    vec3(-1.0,  0.0,  0.0),   // 3: -Y face -> tangent = -X
    vec3( 1.0,  0.0,  0.0),   // 4: +Z face -> tangent = +X
    vec3( 1.0,  0.0,  0.0)    // 5: -Z face -> tangent = +X
);

void main() {
    // Unpack position from attr_vertex
    // Mode 30: bits 0-6 X (0-127), bits 7-13 Y (0-127), bits 14-22 Z (0-511)
    float localX = float(inAttrVertex & 0x7Fu);
    float localY = float((inAttrVertex >> 7u) & 0x7Fu);
    float localZ = float((inAttrVertex >> 14u) & 0x1FFu);

    // Scale local position by stb transform scale
    vec3 localPos = vec3(localX, localY, localZ) * chunk.scale.xyz;

    // World position = chunk offset + scaled local position
    vec3 worldPos = localPos + chunk.chunkOffset.xyz;
    outWorldPos = worldPos;

    // Transform to view space
    vec4 viewPos = camera.view * vec4(worldPos, 1.0);
    outViewPos = viewPos.xyz;

    // Unpack normal index from attr_face (bits 0-4)
    uint normalIdx = inAttrFace & 0x1Fu;
    if (normalIdx > 5u) normalIdx = 0u;  // Clamp to valid range

    // Get world-space normal (voxel chunks have no rotation, normals are axis-aligned)
    vec3 worldNormal = FACE_NORMALS[normalIdx];

    // Transform normal to view space
    // Since voxel chunks have no rotation (only translation + uniform scale),
    // we can use the mat3 of the view matrix directly
    outViewNormal = normalize(mat3(camera.view) * worldNormal);

    // Get tangent for normal mapping
    vec3 worldTangent = FACE_TANGENTS[normalIdx];

    // Transform tangent to view space (same logic as normal)
    outViewTangent = vec4(normalize(mat3(camera.view) * worldTangent), 1.0);

    // Generate texture coordinates based on face normal
    // Project local position onto the face plane
    // UV tiles every 1.0 world units for voxels
    const float UV_SCALE = 1.0;
    if (abs(worldNormal.x) > 0.5) {
        // X-facing faces: project YZ
        outTexCoord = localPos.yz * UV_SCALE;
    } else if (abs(worldNormal.y) > 0.5) {
        // Y-facing faces: project XZ
        outTexCoord = localPos.xz * UV_SCALE;
    } else {
        // Z-facing faces: project XY
        outTexCoord = localPos.xy * UV_SCALE;
    }

    // Transform to clip space
    gl_Position = camera.projection * viewPos;
}
