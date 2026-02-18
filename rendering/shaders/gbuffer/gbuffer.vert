#version 460 core

/**
 * G-buffer geometry pass - vertex shader
 * 
 * Transforms vertices and outputs data needed for deferred rendering:
 * - View-space position
 * - World-space position (for lighting calculations)
 * - View-space normal and tangent (for normal mapping)
 * - Texture coordinates
 */

// Vertex inputs (PBR vertex format - Vertex3DLit)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// Outputs to fragment shader
layout(location = 0) out vec3 outViewPos;
layout(location = 1) out vec3 outViewNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outViewTangent;
layout(location = 4) out vec3 outWorldPos;

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;  // x=near, y=far, z=fov, w=aspect
} camera;

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;  // Inverse transpose of model-view
} pc;

void main() {
    // World position (for lighting pass)
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    
    // Transform to view space
    vec4 viewPos = camera.view * worldPos;
    outViewPos = viewPos.xyz;
    
    // Transform normal to view space using normal matrix
    outViewNormal = normalize(mat3(pc.normalMatrix) * inNormal);
    
    // Transform tangent to view space
    outViewTangent = vec4(normalize(mat3(pc.normalMatrix) * inTangent.xyz), inTangent.w);
    
    // Pass through texture coordinates
    outTexCoord = inTexCoord;
    
    // Transform to clip space
    gl_Position = camera.projection * viewPos;
}
