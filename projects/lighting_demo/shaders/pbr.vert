#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// Uniforms - Set 0 (Global)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 viewPos;
} camera;

// Push constants for model transform
layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragTangent;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    gl_Position = camera.projection * camera.view * worldPos;
    
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent.xyz = normalize(normalMatrix * inTangent.xyz);
    fragTangent.w = inTangent.w;
    fragTexCoord = inTexCoord;
}
