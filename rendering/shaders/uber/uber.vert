/**
 * @file uber.vert
 * @brief Canonical vertex shader with proper MVP transforms
 * 
 * Uses push constants for model matrix and material properties.
 * Camera data remains in a UBO (per-frame data).
 * Passes all necessary data to fragment shader to avoid UBO duplication.
 */
#version 450

// Vertex input (matches Vertex3D - 32 bytes stride)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Camera uniforms (per-frame, set 0 binding 0)
layout(set = 0, binding = 0) uniform CameraUniforms {
    mat4 view;
    mat4 projection;
    vec4 sunDirIntensity;    // xyz = direction, w = intensity
    vec4 sunColor;           // rgb = color, a = unused
    vec4 ambientColor;       // rgb = color, a = intensity
} camera;

// Push constants (per-object, fast updates)
// Total: 96 bytes (must be <= 128 bytes for all backends)
layout(push_constant) uniform PushConstants {
    mat4 model;           // 64 bytes
    vec4 baseColor;       // 16 bytes: rgb = color, a = alpha
    vec4 materialProps;   // 16 bytes: x = metallic, y = roughness, z = unused, w = unused
} pc;

// Outputs to fragment shader (pass all data to avoid UBO in frag)
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragColor;          // rgb = color, a = alpha
layout(location = 4) out vec4 fragMaterialProps;  // x = metallic, y = roughness
layout(location = 5) out vec4 fragSunDirIntensity;
layout(location = 6) out vec4 fragSunColor;
layout(location = 7) out vec4 fragAmbientColor;
layout(location = 8) out vec3 fragCameraPos;

void main() {
    // Transform to world space
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (using upper-left 3x3 of model matrix)
    fragNormal = mat3(pc.model) * inNormal;
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Pass material properties to fragment shader
    fragColor = pc.baseColor;
    fragMaterialProps = pc.materialProps;
    
    // Pass lighting data to fragment (avoids UBO in fragment shader)
    fragSunDirIntensity = camera.sunDirIntensity;
    fragSunColor = camera.sunColor;
    fragAmbientColor = camera.ambientColor;
    
    // Extract camera position from view matrix
    fragCameraPos = -transpose(mat3(camera.view)) * camera.view[3].xyz;
    
    // Transform to clip space: projection * view * worldPos
    gl_Position = camera.projection * camera.view * worldPos;
}
