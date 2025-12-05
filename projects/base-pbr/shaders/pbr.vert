#version 450

// Vertex attributes (location matches Vertex3DLit)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;  // xyz = tangent, w = handedness

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
layout(location = 3) out vec4 fragTangent;  // xyz = tangent, w = handedness

void main() {
    // Transform position to world space
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform position to clip space
    gl_Position = camera.projection * camera.view * worldPos;

    // Transform normal to world space (use inverse transpose for correct transformation)
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal = normalize(normalMatrix * inNormal);

    // Transform tangent to world space (use same normal matrix)
    fragTangent.xyz = normalize(normalMatrix * inTangent.xyz);
    fragTangent.w = inTangent.w;

    // Pass through texture coordinates (no flip needed)
    fragTexCoord = inTexCoord;
}
