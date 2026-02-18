#version 450

// Simple vertex shader for landscape (no fancy descriptors)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(push_constant) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
} mvp;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragColor;

void main() {
    vec4 worldPos = mvp.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(mvp.model) * inNormal;
    
    // Color based on height (green terrain)
    float height = worldPos.y;
    fragColor = mix(vec3(0.3, 0.5, 0.2), vec3(0.5, 0.7, 0.3), clamp(height / 50.0, 0.0, 1.0));
    
    gl_Position = mvp.projection * mvp.view * worldPos;
}

