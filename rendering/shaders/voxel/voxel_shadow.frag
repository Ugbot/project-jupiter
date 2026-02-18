#version 460 core

/**
 * Voxel fragment shader with shadow mapping
 *
 * Extends voxel_simple.frag with:
 * - Shadow map sampling
 * - PCF soft shadows
 * - Slope-based shadow bias
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

// Shadow UBO
layout(set = 0, binding = 2) uniform ShadowUBO {
    mat4 lightSpaceMatrix;
    vec4 lightPosition;
    float shadowMinBias;
    float shadowMaxBias;
    float shadowNearPlane;
    float shadowFarPlane;
} shadow;

// Shadow map sampler (comparison sampler for hardware PCF)
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap;

// Constants
const float SHADOW_AMBIENT = 0.3;  // Minimum light in shadows
const int PCF_RANGE = 1;           // 3x3 kernel

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

/**
 * Calculate slope-based shadow bias
 */
float calculateBias(vec3 N, vec3 L) {
    float NdotL = max(dot(N, L), 0.0);
    return max(shadow.shadowMaxBias * (1.0 - NdotL), shadow.shadowMinBias);
}

/**
 * PCF shadow sampling
 */
float sampleShadow(vec3 projCoords, float bias) {
    ivec2 texDim = textureSize(shadowMap, 0).xy;
    float dx = 1.0 / float(texDim.x);
    float dy = 1.0 / float(texDim.y);

    float shadowFactor = 0.0;
    int count = 0;

    for (int x = -PCF_RANGE; x <= PCF_RANGE; x++) {
        for (int y = -PCF_RANGE; y <= PCF_RANGE; y++) {
            vec2 offset = vec2(dx * float(x), dy * float(y));
            // Hardware comparison: returns 1 if fragment is lit, 0 if in shadow
            float shadowSample = texture(shadowMap, vec3(projCoords.xy + offset, projCoords.z - bias));
            shadowFactor += shadowSample;
            count++;
        }
    }

    return shadowFactor / float(count);
}

/**
 * Full shadow calculation
 */
float calculateShadow(vec3 worldPos, vec3 N, vec3 L) {
    // Transform to light space
    vec4 lightSpacePos = shadow.lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform from [-1,1] to [0,1]
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Outside shadow map = fully lit
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    // Calculate bias and sample
    float bias = calculateBias(N, L);
    float shadowFactor = sampleShadow(projCoords, bias);

    // Mix between shadow and lit (allow some ambient in shadows)
    return mix(SHADOW_AMBIENT, 1.0, shadowFactor);
}

void main() {
    // Get face normal
    uint normalIdx = min(inNormalIndex, 5u);
    vec3 N = FACE_NORMALS[normalIdx];

    // Get material color
    uint matIdx = min(inMaterialIndex, 7u);
    vec3 albedo = MATERIAL_COLORS[matIdx];

    // Light direction (pointing toward light)
    vec3 L = -normalize(light.sunDirection.xyz);
    float NdotL = max(dot(N, L), 0.0);

    // DEBUG: Visualize light-space depth
    vec4 lightSpacePos = shadow.lightSpaceMatrix * vec4(inWorldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Debug mode: visualize depth as color
    // Red = fragment depth (projCoords.z), should be 0-1
    // If depth is negative or > 1, something is wrong with the projection
    #if 0  // Set to 1 to enable debug visualization
    outColor = vec4(projCoords.z, 0.0, 1.0 - projCoords.z, 1.0);  // Red=near, Blue=far
    return;
    #endif

    // Calculate shadow
    float shadowFactor = calculateShadow(inWorldPos, N, L);

    // Half-lambert for softer diffuse
    float halfLambert = NdotL * 0.5 + 0.5;
    halfLambert = halfLambert * halfLambert;

    // Combine lighting with shadow
    vec3 diffuse = albedo * light.sunColor.rgb * light.sunDirection.w * halfLambert * shadowFactor;
    vec3 ambient = albedo * light.ambientColor.rgb;

    // Apply ambient occlusion
    float ao = max(inAO, 0.1);
    vec3 color = (ambient + diffuse) * ao;

    // DEBUG: Visualize AO directly - set to 1 to enable
    #if 0
    // Red = high AO (dark), Blue = low AO (lit)
    // If all green, AO is exactly 0
    outColor = vec4(inAO, 1.0 - inAO, 0.0, 1.0);
    return;
    #endif

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
