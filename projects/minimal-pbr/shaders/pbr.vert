#version 450

// Vertex attributes (matching Vertex3DLit)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;  // xyz = tangent, w = handedness

// Scene uniform buffer (Set 0, Binding 0)
// NOTE: Layout matches RenderGlobals::CameraUBO - view first, then projection
layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 projection;
    vec3 camPos;
    float _pad0;
    vec4 lightDir;      // xyz = direction (towards light), w = intensity
    vec4 lightColor;    // xyz = color, w = unused
} scene;

// Push constants for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    uint textureFlags;
    float _pad;
} push;

// Outputs to fragment shader
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;
layout(location = 3) out vec4 outTangent;

void main() {
    // Transform to world space
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;

    // Transform normal (use inverse transpose for correct non-uniform scaling)
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    outNormal = normalize(normalMatrix * inNormal);

    // Transform tangent (same as normal, preserve handedness in w)
    outTangent.xyz = normalize(normalMatrix * inTangent.xyz);
    outTangent.w = inTangent.w;

    // Pass through texture coordinates
    outTexCoord = inTexCoord;

    // Transform to clip space
    gl_Position = scene.projection * scene.view * worldPos;
}
