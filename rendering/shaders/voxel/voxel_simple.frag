#version 460 core

/**
 * Simple voxel fragment shader
 *
 * Basic directional lighting with ambient occlusion.
 * No PBR textures - just solid colors per material.
 */

// Input from vertex shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) flat in uint inNormalIndex;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in float inAO;
layout(location = 4) flat in uint inMaterialIndex;

// Output
layout(location = 0) out vec4 outColor;

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

// Light UBO
layout(set = 0, binding = 1) uniform LightUBO {
    vec4 sunDirection;   // xyz = direction (normalized), w = intensity
    vec4 sunColor;       // rgb = color, a = unused
    vec4 ambientColor;   // rgb = ambient, a = unused
} light;

// Face normals lookup table
// Matches FaceDirection enum: FACE_POS_X=0, FACE_NEG_X=1, FACE_POS_Y=2, FACE_NEG_Y=3, FACE_POS_Z=4, FACE_NEG_Z=5
const vec3 FACE_NORMALS[6] = vec3[6](
    vec3( 1.0,  0.0,  0.0),   // 0: +X
    vec3(-1.0,  0.0,  0.0),   // 1: -X
    vec3( 0.0,  1.0,  0.0),   // 2: +Y
    vec3( 0.0, -1.0,  0.0),   // 3: -Y
    vec3( 0.0,  0.0,  1.0),   // 4: +Z
    vec3( 0.0,  0.0, -1.0)    // 5: -Z
);

// Material colors (simple palette)
const vec3 MATERIAL_COLORS[8] = vec3[8](
    vec3(0.0, 0.0, 0.0),      // 0: Air (black, shouldn't render)
    vec3(0.5, 0.5, 0.5),      // 1: Stone (gray)
    vec3(0.55, 0.35, 0.2),    // 2: Dirt (brown)
    vec3(0.3, 0.6, 0.2),      // 3: Grass (green)
    vec3(0.9, 0.85, 0.6),     // 4: Sand (tan)
    vec3(0.2, 0.4, 0.8),      // 5: Water (blue)
    vec3(0.6, 0.4, 0.2),      // 6: Wood (brown)
    vec3(0.2, 0.5, 0.15)      // 7: Leaves (dark green)
);

void main() {
    // Get face normal
    uint normalIdx = min(inNormalIndex, 5u);
    vec3 N = FACE_NORMALS[normalIdx];

    // Get material color
    uint matIdx = min(inMaterialIndex, 7u);
    vec3 albedo = MATERIAL_COLORS[matIdx];

    // Simple directional lighting
    vec3 L = -normalize(light.sunDirection.xyz);
    float NdotL = max(dot(N, L), 0.0);

    // Half-lambert for softer shadows
    float halfLambert = NdotL * 0.5 + 0.5;
    halfLambert = halfLambert * halfLambert;

    // Combine lighting
    vec3 diffuse = albedo * light.sunColor.rgb * light.sunDirection.w * halfLambert;
    vec3 ambient = albedo * light.ambientColor.rgb;

    // Apply ambient occlusion
    float ao = max(inAO, 0.1);
    vec3 color = (ambient + diffuse) * ao;

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
