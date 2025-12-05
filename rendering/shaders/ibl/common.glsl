// Common IBL utility functions
// Based on Epic Games' "Real Shading in Unreal Engine 4" and hellovulkan reference

#ifndef IBL_COMMON_GLSL
#define IBL_COMMON_GLSL

const float PI = 3.14159265359;

// Hammersley sequence for quasi-random sampling
// Uses Van der Corput sequence for low-discrepancy sampling
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 Hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

// GGX importance sampling
// Returns halfway vector H sampled from GGX distribution
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    // Sample halfway vector in spherical coordinates
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Convert to Cartesian coordinates (tangent space)
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Convert from tangent space to world space
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

// Schlick-GGX geometry function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / max(denom, 0.0001);
}

// Smith's method for geometry obstruction
float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

// Convert direction vector to UV coordinates for equirectangular mapping
vec2 DirToEquirectUV(vec3 dir) {
    vec2 uv = vec2(atan(dir.z, dir.x), asin(dir.y));
    uv *= vec2(0.1591, 0.3183); // inverse atan
    uv += 0.5;
    return uv;
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

#endif // IBL_COMMON_GLSL
