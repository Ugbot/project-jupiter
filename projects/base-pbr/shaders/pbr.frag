#version 450
#extension GL_GOOGLE_include_directive : require

/**
 * Base PBR Fragment Shader - Using Jupiter Unified Includes
 * 
 * This shader demonstrates the Jupiter engine's unified shader system.
 */

// Engine's unified shader includes (path set via CMake -I flag)
#include "constants.glsl"
#include "pbr_functions.glsl"
#include "tonemap.glsl"

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;

// Uniforms - Set 0 (Global)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 viewPos;
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

// Uniforms - Set 1 (Material)
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D occlusionMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

// Push constants for runtime tuning
layout(push_constant) uniform PushConstants {
    float directLightIntensity;
    float ambientIntensity;
    float emissiveIntensity;
    float exposure;
    uint flags;
} pc;

// Flag bits for debug/feature toggles
const uint FLAG_DEBUG_NORMALS = 1u << 0;
const uint FLAG_DEBUG_ALBEDO = 1u << 1;
const uint FLAG_DEBUG_METALLIC = 1u << 2;
const uint FLAG_DEBUG_ROUGHNESS = 1u << 3;
const uint FLAG_USE_ACES = 1u << 4;

// Output
layout(location = 0) out vec4 outColor;

// Normal mapping helper
vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;

    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * fragTangent.w;
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

// Calculate PBR lighting
vec3 calculatePBR(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness,
                  vec3 lightColor, float lightIntensity) {
    vec3 H = normalize(V + L);

    vec3 F0 = CalculateF0(albedo, metallic, 0.04);

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
    float roughness = max(metallicRoughnessSample.g, MIN_ROUGHNESS);
    float metallic = metallicRoughnessSample.b;

    if ((pc.flags & FLAG_DEBUG_METALLIC) != 0u) {
        outColor = vec4(vec3(metallic), 1.0);
        return;
    }
    if ((pc.flags & FLAG_DEBUG_ROUGHNESS) != 0u) {
        outColor = vec4(vec3(roughness), 1.0);
        return;
    }

    vec3 N = getNormalFromMap();
    vec3 V = normalize(camera.viewPos - fragWorldPos);

    // Accumulate lighting
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

        Lo += calculatePBR(N, V, L, albedo, metallic, roughness, lightColor, lightIntensity) * attenuation;
    }

    Lo *= pc.directLightIntensity;

    // IBL ambient
    vec3 F0 = CalculateF0(albedo, metallic, 0.04);
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

    float ao = texture(occlusionMap, fragTexCoord).r;
    vec3 emissive = GammaToLinear(texture(emissiveMap, fragTexCoord).rgb);

    vec3 ambientLight = lights.ambientColor.rgb * lights.ambientColor.w;
    vec3 ambient = (kD * diffuse + specular) * ao;
    ambient += albedo * ambientLight * ao * 0.1;
    ambient *= pc.ambientIntensity;

    // Combine
    vec3 color = ambient + Lo + emissive * pc.emissiveIntensity;

    // Apply exposure
    color *= pc.exposure;

    // Tonemapping
    if ((pc.flags & FLAG_USE_ACES) != 0u) {
        color = TonemapACES(color);
    } else {
        color = TonemapReinhard(color);
    }

    // Gamma correction
    color = LinearToSRGB(color);

    outColor = vec4(color, alpha);
}
