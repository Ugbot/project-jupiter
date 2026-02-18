#version 460 core

/**
 * Smooth terrain vertex shader for SmoothVertex format
 *
 * SmoothVertex (32 bytes):
 * - position: vec3 (12 bytes)
 * - normal: vec3 (12 bytes)
 * - materialId: uint8 (1 byte)
 * - ao: uint8 (1 byte)
 * - texBlendU: uint8 (1 byte)
 * - texBlendV: uint8 (1 byte)
 * - secondaryMaterialId: uint8 (1 byte)
 * - blendFactor: uint8 (1 byte)
 * - padding: 2 bytes
 */

// Vertex inputs (unpacked for clarity)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in uint inPackedMaterial;  // materialId + ao + texBlend packed

// Output to fragment shader
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out float outAO;
layout(location = 3) flat out uint outMaterialId;
layout(location = 4) out vec2 outTexCoord;

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
    vec4 scale;        // xyz = scale, w = unused
} chunk;

void main() {
    // World position = chunk offset + local position
    vec3 worldPos = chunk.chunkOffset.xyz + inPosition * chunk.scale.xyz;
    
    // Transform to clip space
    gl_Position = camera.viewProjection * vec4(worldPos, 1.0);
    
    // Unpack material data
    // materialId in bits 0-7, ao in bits 8-15, texBlendU in bits 16-23, texBlendV in bits 24-31
    uint materialId = inPackedMaterial & 0xFFu;
    float ao = float((inPackedMaterial >> 8) & 0xFFu) / 255.0;
    float texU = float((inPackedMaterial >> 16) & 0xFFu) / 255.0;
    float texV = float((inPackedMaterial >> 24) & 0xFFu) / 255.0;
    
    // Pass to fragment shader
    outWorldPos = worldPos;
    outNormal = normalize(inNormal);
    outAO = ao;
    outMaterialId = materialId;
    outTexCoord = vec2(texU, texV);
}



