#version 450
#extension GL_GOOGLE_include_directive : require

/**
 * Minimal PBR Fragment Shader - Using Jupiter Unified Includes
 * 
 * This demo uses the engine's unified shader include system,
 * demonstrating how projects can leverage shared shader code.
 */

// Use engine's unified shader includes (path set via CMake -I flag)
#include "constants.glsl"
#include "pbr_functions.glsl"
#include "tonemap.glsl"
#include "normal_mapping.glsl"

// Inputs from vertex shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// Scene uniform buffer (Set 0, Binding 0)
layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 projection;
    vec3 camPos;
    float _pad0;
    vec4 lightDir;      // xyz = direction (towards light), w = intensity
    vec4 lightColor;    // xyz = color, w = unused
} scene;

// PBR Textures (Set 1)
layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 3) uniform sampler2D occlusionTex;
layout(set = 1, binding = 4) uniform sampler2D emissiveTex;

// Material from push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    uint textureFlags;
    float exposure;  // Added for runtime exposure control
} material;

// Output
layout(location = 0) out vec4 outColor;

// Debug mode constants
const int DEBUG_NONE = 0;
const int DEBUG_NORMALS = 1;
const int DEBUG_METALLIC = 2;
const int DEBUG_ROUGHNESS = 3;
const int DEBUG_BASECOLOR = 4;

const int debugMode = DEBUG_NONE;

// Helper to check texture flags
bool hasBaseColorTex() { return (material.textureFlags & 1u) != 0u; }
bool hasNormalTex() { return (material.textureFlags & 2u) != 0u; }
bool hasMetallicRoughnessTex() { return (material.textureFlags & 4u) != 0u; }
bool hasOcclusionTex() { return (material.textureFlags & 8u) != 0u; }
bool hasEmissiveTex() { return (material.textureFlags & 16u) != 0u; }

// Get normal from normal map
vec3 getNormal() {
    vec3 N = normalize(inNormal);

    if (hasNormalTex()) {
        vec3 T = normalize(inTangent.xyz);
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T) * inTangent.w;
        mat3 TBN = mat3(T, B, N);
        
        vec3 normalMap = texture(normalTex, inTexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * normalMap);
    }

    return N;
}

void main() {
    // Get base color (from texture or factor)
    vec4 baseColor;
    if (hasBaseColorTex()) {
        vec4 texColor = texture(baseColorTex, inTexCoord);
        baseColor = vec4(GammaToLinear(texColor.rgb), texColor.a) * material.baseColorFactor;
    } else {
        baseColor = material.baseColorFactor;
    }

    // Get metallic and roughness
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (hasMetallicRoughnessTex()) {
        vec4 mrSample = texture(metallicRoughnessTex, inTexCoord);
        roughness = mrSample.g * roughness;
        metallic = mrSample.b * metallic;
    }

    roughness = max(roughness, MIN_ROUGHNESS);
    float alphaRoughness = AlphaDirectLighting(roughness);

    // Get ambient occlusion
    float ao = 1.0;
    if (hasOcclusionTex()) {
        ao = texture(occlusionTex, inTexCoord).r;
    }

    // Get emissive
    vec3 emissive = vec3(0.0);
    if (hasEmissiveTex()) {
        emissive = GammaToLinear(texture(emissiveTex, inTexCoord).rgb);
    }

    // F0 calculation using engine function
    vec3 f0 = CalculateF0(baseColor.rgb, metallic, 0.04);
    vec3 diffuseColor = baseColor.rgb * (vec3(1.0) - vec3(0.04)) * (1.0 - metallic);

    // Vectors
    vec3 N = getNormal();
    vec3 V = normalize(scene.camPos - inWorldPos);
    vec3 L = normalize(scene.lightDir.xyz);
    vec3 H = normalize(L + V);

    // Dot products
    float NoL = max(dot(N, L), 0.001);
    float NoV = max(dot(N, V), 0.001);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);

    // Debug modes
    if (debugMode == DEBUG_NORMALS) {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    } else if (debugMode == DEBUG_METALLIC) {
        outColor = vec4(vec3(metallic), 1.0);
        return;
    } else if (debugMode == DEBUG_ROUGHNESS) {
        outColor = vec4(vec3(roughness), 1.0);
        return;
    } else if (debugMode == DEBUG_BASECOLOR) {
        outColor = vec4(baseColor.rgb, 1.0);
        return;
    }

    // PBR BRDF using engine functions
    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoL, NoV, alphaRoughness);
    vec3 F = FresnelSchlick(HoV, f0);

    // Diffuse contribution
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = Diffuse(kD * baseColor.rgb);

    // Specular contribution (Cook-Torrance)
    vec3 specular = (D * G * F) / (4.0 * NoL * NoV + 0.0001);

    // Direct lighting
    float lightIntensity = scene.lightDir.w;
    vec3 lightColor = scene.lightColor.rgb;
    vec3 color = NoL * lightColor * lightIntensity * (diffuse + specular);

    // Apply AO
    color *= ao;

    // Simple ambient
    vec3 ambient = vec3(0.03) * baseColor.rgb * ao;
    color += ambient;

    // Add emissive
    color += emissive;

    // Apply exposure
    float exposure = material.exposure > 0.0 ? material.exposure : DEFAULT_EXPOSURE;
    color *= exposure;

    // Tonemapping (simple Reinhard)
    color = TonemapReinhard(color);

    // Gamma correction (if needed - check swapchain format)
    // color = LinearToSRGB(color);

    outColor = vec4(color, baseColor.a);
}
