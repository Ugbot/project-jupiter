/**
 * @file pbr.vert
 * @brief PBR Vertex Shader
 * 
 * Transforms vertices and passes data to fragment shader for PBR lighting.
 */
#version 450

// Vertex input (32 bytes stride)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Camera uniforms (set 0, binding 0)
layout(set = 0, binding = 0) uniform CameraUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = unused
} camera;

// Model matrix as push constant
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragViewDir;

void main() {
    // Transform to world space
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (using inverse transpose for non-uniform scale)
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // View direction (from surface to camera)
    fragViewDir = normalize(camera.cameraPosition.xyz - worldPos.xyz);
    
    // Transform to clip space
    gl_Position = camera.viewProjection * worldPos;
}



