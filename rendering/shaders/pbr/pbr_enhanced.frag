#version 460 core
#extension GL_GOOGLE_include_directive : require

/**
 * Enhanced PBR Fragment Shader (Jupiter Engine)
 * 
 * Uses unified shader include system for:
 * - PBR BRDF functions
 * - Tonemapping operators
 * - Shadow mapping
 * - Normal mapping
 * 
 * Feature flags via specialization constants:
 * - SHADOW_ENABLED
 * - SSAO_ENABLED
 */

// Include unified shader libraries
#include "../includes/constants.glsl"
#include "../includes/pbr_functions.glsl"
#include "../includes/tonemap.glsl"
#include "../includes/normal_mapping.glsl"

// Feature flags (specialization constants)
layout(constant_id = 0) const int SHADOW_ENABLED = 0;
layout(constant_id = 1) const int SSAO_ENABLED = 0;

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec4 fragShadowCoord;

// Set 0: Global/Camera/Lights
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

layout(set = 0, binding = 1) uniform LightUBO {
    vec4 lightPositions[16];
    vec4 lightColors[16];
    vec4 ambientColor;
    int numLights;
} lights;

// IBL resources
layout(set = 0, binding = 2) uniform samplerCube irradianceMap;
layout(set = 0, binding = 3) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 4) uniform sampler2D brdfLUT;

// Set 1: Material textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D occlusionMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

// Set 2: Shadow/SSAO (optional)
layout(set = 2, binding = 0) uniform sampler2DShadow shadowMap;
layout(set = 2, binding = 1) uniform sampler2D ssaoMap;
layout(set = 2, binding = 2) uniform ShadowUBO {
    mat4 lightSpaceMatrix;
    vec4 lightPosition;
    float shadowMinBias;
    float shadowMaxBias;
    float shadowNearPlane;
    float shadowFarPlane;
} shadow;

// Push constants for runtime tuning
layout(push_constant) uniform PushConstants {
    float directLightIntensity;
    float ambientIntensity;
    float emissiveIntensity;
    float roughnessOverride;
    float metallicOverride;
    float baseReflectivity;
    float exposure;
    float shadowIntensity;
    uint flags;
} pc;

// Flag bits
const uint FLAG_USE_ROUGHNESS_OVERRIDE = 1u << 0;
const uint FLAG_USE_METALLIC_OVERRIDE = 1u << 1;
const uint FLAG_DISABLE_IBL = 1u << 2;
const uint FLAG_DISABLE_NORMAL_MAPPING = 1u << 3;
const uint FLAG_DEBUG_NORMALS = 1u << 4;
const uint FLAG_DEBUG_ALBEDO = 1u << 5;
const uint FLAG_DEBUG_METALLIC = 1u << 6;
const uint FLAG_DEBUG_ROUGHNESS = 1u << 7;
const uint FLAG_DEBUG_AO = 1u << 8;
const uint FLAG_USE_ACES = 1u << 9;
const uint FLAG_USE_GT = 1u << 10;

layout(location = 0) out vec4 outColor;

// ============================================================================
// Shadow Functions
// ============================================================================

float calculateShadowBias(vec3 normal, vec3 lightDir) {
    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    return max(shadow.shadowMaxBias * (1.0 - cosTheta), shadow.shadowMinBias);
}

float shadowPCF(vec4 shadowCoord, float bias) {
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadowVal = 0.0;

    // 3x3 PCF kernel
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            shadowVal += texture(shadowMap, vec3(shadowCoord.xy + offset, shadowCoord.z - bias));
        }
    }

    return shadowVal / 9.0;
}

float getShadowFactor(vec3 worldPos, vec3 normal) {
    if (SHADOW_ENABLED == 0) return 1.0;

    vec4 shadowCoord = shadow.lightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

    vec3 lightDir = normalize(shadow.lightPosition.xyz - worldPos);
    float bias = calculateShadowBias(normal, lightDir);

    float shadowFactor = shadowPCF(shadowCoord, bias);
    
    // Apply shadow intensity from push constants
    return mix(SHADOW_AMBIENT, 1.0, shadowFactor * pc.shadowIntensity);
}

// ============================================================================
// SSAO Functions  
// ============================================================================

float getSSAOFactor(vec2 screenCoord) {
    if (SSAO_ENABLED == 0) return 1.0;
    return texture(ssaoMap, screenCoord).r;
}

// ============================================================================
// Local Normal Mapping Helper (uses included TBN calculation)
// ============================================================================

vec3 getNormalFromMap() {
    if ((pc.flags & FLAG_DISABLE_NORMAL_MAPPING) != 0u) {
        return normalize(fragNormal);
    }
    
    vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;

    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * fragTangent.w;
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

// ============================================================================
// PBR Direct Lighting
// ============================================================================

vec3 calculateDirectLight(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, 
                          float roughness, vec3 lightColor, float lightIntensity) {
    vec3 H = normalize(V + L);

    vec3 F0 = vec3(pc.baseReflectivity);
    F0 = mix(F0, albedo, metallic);

    float alphaRoughness = AlphaDirectLighting(roughness);
    float NoH = max(dot(N, H), 0.0);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0);
    float HoV = max(dot(H, V), 0.0);
    
    float D = DistributionGGX(NoH, roughness);
    float G = GeometrySchlickGGX(NoL, NoV, alphaRoughness);
    vec3 F = FresnelSchlick(HoV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NoV * NoL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    vec3 diffuse = Diffuse(kD * albedo);

    return (diffuse + specular) * lightColor * lightIntensity * NoL;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // Debug modes
    if ((pc.flags & FLAG_DEBUG_NORMALS) != 0u) {
        vec3 N = getNormalFromMap();
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    // Sample material textures
    vec4 albedoSample = texture(albedoMap, fragTexCoord);
    vec3 albedo = GammaToLinear(albedoSample.rgb);
    float alpha = albedoSample.a;

    if ((pc.flags & FLAG_DEBUG_ALBEDO) != 0u) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    vec3 metallicRoughnessSample = texture(metallicRoughnessMap, fragTexCoord).rgb;
    float roughness = clamp(metallicRoughnessSample.g, MIN_ROUGHNESS, 1.0);
    float metallic = metallicRoughnessSample.b;

    // Apply overrides
    if ((pc.flags & FLAG_USE_ROUGHNESS_OVERRIDE) != 0u) {
        roughness = clamp(pc.roughnessOverride, MIN_ROUGHNESS, 1.0);
    }
    if ((pc.flags & FLAG_USE_METALLIC_OVERRIDE) != 0u) {
        metallic = pc.metallicOverride;
    }

    if ((pc.flags & FLAG_DEBUG_METALLIC) != 0u) {
        outColor = vec4(vec3(metallic), 1.0);
        return;
    }
    if ((pc.flags & FLAG_DEBUG_ROUGHNESS) != 0u) {
        outColor = vec4(vec3(roughness), 1.0);
        return;
    }

    vec3 N = getNormalFromMap();
    vec3 V = normalize(camera.cameraPosition.xyz - fragWorldPos);

    // Shadow factor
    float shadowFactor = getShadowFactor(fragWorldPos, N);

    // SSAO factor
    vec4 clipPos = camera.viewProjection * vec4(fragWorldPos, 1.0);
    vec2 screenCoord = (clipPos.xy / clipPos.w) * 0.5 + 0.5;
    screenCoord.y = 1.0 - screenCoord.y;
    float ssaoFactor = getSSAOFactor(screenCoord);

    // Direct lighting
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < lights.numLights && i < 16; i++) {
        vec3 lightPos = lights.lightPositions[i].xyz;
        float lightType = lights.lightPositions[i].w;
        vec3 lightColor = lights.lightColors[i].xyz;
        float lightIntensity = lights.lightColors[i].w;

        vec3 L;
        float attenuation = 1.0;

        if (lightType == 0.0) {
            L = normalize(lightPos);
        } else {
            vec3 lightDir = lightPos - fragWorldPos;
            float distance = length(lightDir);
            L = lightDir / distance;
            attenuation = 1.0 / max(distance * distance, 0.01);
        }

        float lightShadow = (i == 0 && lightType == 0.0) ? shadowFactor : 1.0;

        Lo += calculateDirectLight(N, V, L, albedo, metallic, roughness, lightColor, lightIntensity) 
              * attenuation * lightShadow;
    }

    Lo *= pc.directLightIntensity;

    // IBL ambient (if enabled)
    vec3 ambient = vec3(0.0);
    
    if ((pc.flags & FLAG_DISABLE_IBL) == 0u) {
        vec3 F0 = vec3(pc.baseReflectivity);
        F0 = mix(F0, albedo, metallic);

        float NoV = max(dot(N, V), 0.0);
        vec3 F = FresnelSchlickRoughness(NoV, F0, roughness);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 diffuse = irradiance * albedo;

        vec3 R = reflect(-V, N);
        vec3 prefilteredColor = textureLod(prefilteredMap, R, roughness * MAX_REFLECTION_LOD).rgb;
        vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
        vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

        ambient = (kD * diffuse + specular);
    }

    // Material AO and SSAO
    float ao = texture(occlusionMap, fragTexCoord).r;
    float combinedAO = ao * ssaoFactor;

    if ((pc.flags & FLAG_DEBUG_AO) != 0u) {
        outColor = vec4(vec3(combinedAO), 1.0);
        return;
    }

    // Emissive
    vec3 emissive = GammaToLinear(texture(emissiveMap, fragTexCoord).rgb);

    // Ambient from UBO
    vec3 ambientLight = lights.ambientColor.rgb * lights.ambientColor.w;
    ambient += albedo * ambientLight * 0.1;
    ambient *= combinedAO * pc.ambientIntensity;

    // Combine all lighting with exposure
    vec3 color = (ambient + Lo + emissive * pc.emissiveIntensity) * pc.exposure;

    // Tonemapping (select based on flags)
    if ((pc.flags & FLAG_USE_ACES) != 0u) {
        color = TonemapACES(color);
    } else if ((pc.flags & FLAG_USE_GT) != 0u) {
        color = TonemapGT(color);
    } else {
        // Default: simple Reinhard
        color = TonemapReinhard(color);
    }

    // Gamma correction
    color = LinearToSRGB(color);

    outColor = vec4(color, alpha);
}
