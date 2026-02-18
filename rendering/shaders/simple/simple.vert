#version 450

// Vertex input (matches primitives::Vertex)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Uniforms
layout(set = 0, binding = 0) uniform CameraUniforms {
    mat4 view;
    mat4 projection;
} camera;

layout(set = 0, binding = 1) uniform ObjectUniforms {
    mat4 model;
} object;

// Outputs
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = (object.model * vec4(inNormal, 0.0)).xyz;
    fragTexCoord = inTexCoord;
    
    gl_Position = camera.projection * camera.view * worldPos;
}

