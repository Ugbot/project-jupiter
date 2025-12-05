#version 460 core

/**
 * Clustered Forward PBR Vertex Shader
 * 
 * Standard vertex transformation with tangent space for normal mapping.
 */

// ============================================================================
// Inputs
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;  // xyz = tangent, w = handedness

// ============================================================================
// Outputs
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

// Model matrix via push constant
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;  // transpose(inverse(model)) for correct normal transform
} pc;

// ============================================================================
// Main
// ============================================================================

void main() {
    // World space position
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    
    // Transform normal to world space
    outNormal = normalize(mat3(pc.normalMatrix) * inNormal);
    
    // Pass through texture coordinates
    outTexCoord = inTexCoord;
    
    // Transform tangent to world space (keep handedness)
    outTangent = vec4(normalize(mat3(pc.model) * inTangent.xyz), inTangent.w);
    
    // Final clip space position
    gl_Position = camera.viewProjection * worldPos;
}

