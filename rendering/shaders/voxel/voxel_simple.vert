#version 460 core

/**
 * Voxel vertex shader for stb_voxel_render Mode 30 output
 *
 * Mode 30 uses STBVOX_ICONFIG_VERTEX_32_XYZA encoding:
 * - attr_vertex (uint32): X[0:7] Y[8:15] Z[16:23] AO[24:31]
 *   stbvox_vertex_encode(x,y,z,ao,texlerp) = ((x)+((y)<<8)+((z)<<16)+((ao)<<24))
 *
 * - attr_face (uint32): tex1[0:7] tex2[8:15] color[16:23] face_info[24:31]
 *   where face_info = (normal << 2) + facerot, so normal is at bits 2-4 of face_info
 */

// Vertex input (matches VoxelVertexGPU struct - 8 bytes)
layout(location = 0) in uint inAttrVertex;  // Packed position + AO
layout(location = 1) in uint inAttrFace;    // Packed face data (normal, color, tex)

// Output to fragment shader
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) flat out uint outNormalIndex;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out float outAO;
layout(location = 4) flat out uint outMaterialIndex;

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;  // near, far, fovY, aspect
} camera;

// Per-chunk push constant
layout(push_constant) uniform ChunkPushConstant {
    vec4 chunkOffset;  // xyz = world offset for this chunk, w = unused
    vec4 scale;        // xyz = scale from stb transform, w = unused
} chunk;

// Face normals lookup table (6 axis-aligned directions)
// Matches FaceDirection enum: FACE_POS_X=0, FACE_NEG_X=1, FACE_POS_Y=2, FACE_NEG_Y=3, FACE_POS_Z=4, FACE_NEG_Z=5
const vec3 FACE_NORMALS[6] = vec3[6](
    vec3( 1.0,  0.0,  0.0),   // 0: +X
    vec3(-1.0,  0.0,  0.0),   // 1: -X
    vec3( 0.0,  1.0,  0.0),   // 2: +Y
    vec3( 0.0, -1.0,  0.0),   // 3: -Y
    vec3( 0.0,  0.0,  1.0),   // 4: +Z
    vec3( 0.0,  0.0, -1.0)    // 5: -Z
);

void main() {
    // Unpack position from attr_vertex (Mode 30 XYZA format)
    // stbvox_vertex_encode(x,y,z,ao,texlerp) = ((x)+((y)<<8)+((z)<<16)+((ao)<<24))
    // bits 0-7:   X position (0-255)
    // bits 8-15:  Y position (0-255)
    // bits 16-23: Z position (0-255)
    // bits 24-31: Ambient occlusion (0-255)
    float x = float(inAttrVertex & 0xFFu);
    float y = float((inAttrVertex >> 8) & 0xFFu);
    float z = float((inAttrVertex >> 16) & 0xFFu);
    float ao = float((inAttrVertex >> 24) & 0xFFu) / 255.0;

    // Local position within chunk
    // stb outputs half-voxel precision (0-32 range for 16-unit chunks)
    // scale.xyz is typically (0.5, 0.5, 0.5) to convert to voxel units
    vec3 localPos = vec3(x, y, z) * chunk.scale.xyz;

    // World position = chunk offset + local position
    vec3 worldPos = chunk.chunkOffset.xyz + localPos;

    // Transform to clip space
    gl_Position = camera.viewProjection * vec4(worldPos, 1.0);

    // Extract face_info from byte 3 of attr_face
    // face_info = (normal << 2) + facerot
    // So normal is at bits 2-4 of face_info (bits 26-28 of attr_face)
    uint faceInfo = (inAttrFace >> 24) & 0xFFu;
    uint normalIdx = (faceInfo >> 2) & 0x7u;

    // Extract color index from byte 2 (bits 16-23)
    uint matIdx = (inAttrFace >> 16) & 0xFFu;

    // Pass to fragment shader
    outWorldPos = worldPos;
    outNormalIndex = normalIdx;
    outTexCoord = vec2(0.0);  // Not using texcoords for now
    outAO = ao;
    outMaterialIndex = matIdx;
}
