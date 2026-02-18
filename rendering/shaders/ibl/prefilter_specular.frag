#version 450

// Specular environment prefiltering with GGX importance sampling using MRT
// Generates mipmapped prefiltered environment map for varying roughness
// Renders all 6 cubemap faces in a single pass

layout(location = 0) in vec2 texCoord;

// Input: Environment cubemap
layout(set = 0, binding = 0) uniform samplerCube envCubemap;

// Push constants for roughness and sample count
layout(push_constant) uniform PushConstants {
    float roughness;  // Roughness level for this mip
    uint sampleCount; // Number of samples (typically 1024)
} pc;

// Multiple render targets - one per cubemap face
layout(location = 0) out vec4 cubeFace0;
layout(location = 1) out vec4 cubeFace1;
layout(location = 2) out vec4 cubeFace2;
layout(location = 3) out vec4 cubeFace3;
layout(location = 4) out vec4 cubeFace4;
layout(location = 5) out vec4 cubeFace5;

// ============================================================================
// Common IBL Functions
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
vec3 UVToXYZ(int face, vec2 uv) {
    if (face == 0) { 
        return vec3(1.0, uv.y, -uv.x);  // +X
    } else if (face == 1) { 
        return vec3(-1.0, uv.y, uv.x);  // -X
    } else if (face == 2) { 
        return vec3(uv.x, -1.0, uv.y);  // +Y
    } else if (face == 3) { 
        return vec3(uv.x, 1.0, -uv.y);  // -Y
    } else if (face == 4) { 
        return vec3(uv.x, uv.y, 1.0);   // +Z
    } else { 
        return vec3(-uv.x, uv.y, -1.0); // -Z
    }
}

void WriteFace(int face, vec3 colorIn) {
    vec4 color = vec4(colorIn.rgb, 1.0);
    
    if (face == 0) {
        cubeFace0 = color;
    } else if (face == 1) {
        cubeFace1 = color;
    } else if (face == 2) {
        cubeFace2 = color;
    } else if (face == 3) {
        cubeFace3 = color;
    } else if (face == 4) {
        cubeFace4 = color;
    } else {
        cubeFace5 = color;
    }
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
    vec2 texCoordTemp = texCoord * 2.0 - 1.0;
    
    for (int face = 0; face < 6; ++face) {
        vec3 position = UVToXYZ(face, texCoordTemp);
        vec3 reflection = normalize(position);
        
        vec3 prefilteredColor = PrefilterSpecular(reflection);
        WriteFace(face, prefilteredColor);
    }
}
