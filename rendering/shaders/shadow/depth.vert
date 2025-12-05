#version 460 core

/**
 * Shadow map depth pass - vertex shader
 * 
 * Transforms vertices to light space for shadow map generation.
 * Only position is needed - no color, normal, or texture coordinates.
 */

// Vertex input - position only
layout(location = 0) in vec3 inPosition;

// Shadow UBO
layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 lightSpaceMatrix;
    vec4 lightPosition;
    float shadowMinBias;
    float shadowMaxBias;
    float shadowNearPlane;
    float shadowFarPlane;
} shadowUBO;

// Push constants - model matrix
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    // Transform vertex to light space
    gl_Position = shadowUBO.lightSpaceMatrix * pc.model * vec4(inPosition, 1.0);
}

