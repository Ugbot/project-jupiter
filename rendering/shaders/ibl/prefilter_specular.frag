#version 450

// Specular environment prefiltering with GGX importance sampling
// Generates mipmapped prefiltered environment map for varying roughness
// Renders one cubemap face at a time

layout(location = 0) in vec2 texCoord;

// Input: Environment cubemap
layout(set = 0, binding = 0) uniform samplerCube envCubemap;

// Push constants for face index, roughness, and sample count
layout(push_constant) uniform PushConstants {
    int faceIndex;    // 0-5 for +X, -X, +Y, -Y, +Z, -Z
    float roughness;  // Roughness level for this mip
    uint sampleCount; // Number of samples (typically 1024)
} pc;

// Output: Single color attachment (one cubemap face)
layout(location = 0) out vec4 outColor;

// ============================================================================
// Common IBL Functions (inlined)
// ============================================================================

const float PI = 3.14159265359;

// Hammersley sequence for quasi-random sampling
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

// GGX importance sampling
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// GGX normal distribution function
float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0001);
}

// Convert cubemap face and local UV to 3D direction
vec3 UVToDirection(int faceIndex, vec2 uv) {
    // Convert UV from [0,1] to [-1,1]
    vec2 texCoord = uv * 2.0 - 1.0;

    vec3 dir;
    if (faceIndex == 0) {
        // +X
        dir = vec3(1.0, -texCoord.y, -texCoord.x);
    } else if (faceIndex == 1) {
        // -X
        dir = vec3(-1.0, -texCoord.y, texCoord.x);
    } else if (faceIndex == 2) {
        // +Y
        dir = vec3(texCoord.x, 1.0, texCoord.y);
    } else if (faceIndex == 3) {
        // -Y
        dir = vec3(texCoord.x, -1.0, -texCoord.y);
    } else if (faceIndex == 4) {
        // +Z
        dir = vec3(texCoord.x, -texCoord.y, 1.0);
    } else {
        // -Z
        dir = vec3(-texCoord.x, -texCoord.y, -1.0);
    }

    return normalize(dir);
}

// ============================================================================
// Specular Prefiltering
// ============================================================================

// Prefilter environment for specular reflections
vec3 PrefilterSpecular(vec3 R) {
    vec3 N = R;
    vec3 V = R;

    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < pc.sampleCount; ++i) {
        vec2 Xi = Hammersley(i, pc.sampleCount);
        vec3 H = ImportanceSampleGGX(Xi, N, pc.roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0) {
            // Sample from environment
            // For very rough surfaces, sample from lower mip levels to reduce noise
            float D = DistributionGGX(max(dot(N, H), 0.0), pc.roughness);
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;

            // Compute LOD based on pdf and environment size
            vec2 envSize = vec2(textureSize(envCubemap, 0));
            float saTexel = 4.0 * PI / (6.0 * envSize.x * envSize.y);
            float saSample = 1.0 / (float(pc.sampleCount) * pdf + 0.0001);

            float mipLevel = pc.roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            mipLevel = clamp(mipLevel, 0.0, float(textureQueryLevels(envCubemap) - 1));

            prefilteredColor += textureLod(envCubemap, L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    return prefilteredColor / max(totalWeight, 0.0001);
}

void main() {
    // Convert UV to 3D direction for this cubemap face
    vec3 reflection = UVToDirection(pc.faceIndex, texCoord);

    // Prefilter specular reflection
    vec3 prefilteredColor = PrefilterSpecular(reflection);

    outColor = vec4(prefilteredColor, 1.0);
}
