#version 460 core

/**
 * Enhanced PBR Vertex Shader
 * 
 * Outputs shadow coordinates for shadow mapping.
 */

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 fragShadowCoord;

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

// Push constants for model matrix and shadow matrix
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 lightSpaceMatrix;  // For shadow coordinate calculation
} pc;

void main() {
    // World space position
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal and tangent to world space
    mat3 normalMatrix = mat3(transpose(inverse(pc.model)));
    fragNormal = normalMatrix * inNormal;
    fragTangent = vec4(normalMatrix * inTangent.xyz, inTangent.w);
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Calculate shadow coordinates (light space)
    fragShadowCoord = pc.lightSpaceMatrix * worldPos;
    
    // Final clip space position
    gl_Position = camera.viewProjection * worldPos;
}

