#version 450

/*
 * Jupiter PBR Fragment Shader
 * 
 * Features:
 * - Cook-Torrance BRDF with GGX
 * - Image-Based Lighting (IBL)
 * - Poisson disk shadow mapping (soft shadows)
 * - Normal mapping
 * - Material properties UBO
 */

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragTangent;

// Set 0: Global resources
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

layout(set = 0, binding = 2) uniform samplerCube irradianceMap;
layout(set = 0, binding = 3) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 4) uniform sampler2D brdfLUT;

// Shadow map (depth comparison sampler for hardware PCF)
layout(set = 0, binding = 5) uniform sampler2DShadow shadowMap;

// SSAO (placeholder)
layout(set = 0, binding = 6) uniform sampler2D ssaoMap;

// Shadow/Effects UBO
layout(set = 0, binding = 7) uniform ShadowEffectsUBO {
    mat4 lightSpaceMatrix;
    vec4 shadowParams;    // x=minBias, y=maxBias, z=unused, w=unused
    int shadowEnabled;
    int ssaoEnabled;
    float ssaoIntensity;
    int _padding0;
} shadowEffects;

// Set 1: Material resources
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D occlusionMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

layout(set = 1, binding = 5) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    uint alphaMode;
    uint doubleSided;
} material;

// Push constants for runtime tuning
// NOTE: Vertex shader uses bytes 0-63 for model matrix (mat4)
// Fragment shader push constants start at offset 64
layout(push_constant) uniform PushConstants {
    layout(offset = 64) float directLightIntensity;    // Scale all direct lights
    float ambientIntensity;        // Scale ambient/IBL contribution
    float emissiveIntensity;       // Scale emissive contribution
    float roughnessOverride;       // Override roughness (when flag set)
    float metallicOverride;        // Override metallic (when flag set)
    float baseReflectivity;        // F0 for dielectrics
    float exposure;                // Manual exposure multiplier
    float shadowIntensity;         // Shadow darkness
    float maxReflectionLod;        // Max mip level for prefiltered IBL (from HelloVulkan)
    float lightFalloff;            // Light attenuation power (from HelloVulkan)
    float albedoMultiplier;        // Add albedo color to dark scenes (from HelloVulkan)
    uint flags;                    // Feature flags
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
const uint FLAG_DEBUG_UV = 1u << 9;
const uint FLAG_DEBUG_EMISSIVE = 1u << 10;
const uint FLAG_DISABLE_DIRECT_LIGHT = 1u << 11;
const uint FLAG_DISABLE_TONEMAPPING = 1u << 12;
const uint FLAG_DEBUG_DIFFUSE_ONLY = 1u << 13;
const uint FLAG_DEBUG_SPECULAR_ONLY = 1u << 14;
const uint FLAG_DEBUG_F0 = 1u << 15;

layout(location = 0) out vec4 outColor;

// ============================================================================
// sRGB to Linear conversion (from vulkan-gltf-pbr reference)
// Textures are stored as UNORM, we convert manually for correctness
// ============================================================================
vec4 SRGBtoLINEAR(vec4 srgbIn) {
    // Exact sRGB to linear conversion
    vec3 bLess = step(vec3(0.04045), srgbIn.xyz);
    vec3 linOut = mix(srgbIn.xyz / vec3(12.92), 
                      pow((srgbIn.xyz + vec3(0.055)) / vec3(1.055), vec3(2.4)), 
                      bLess);
    return vec4(linOut, srgbIn.w);
}

vec3 SRGBtoLINEAR(vec3 srgbIn) {
    return SRGBtoLINEAR(vec4(srgbIn, 1.0)).rgb;
}

// ============================================================================
// Constants
// ============================================================================

const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.04;
const float DEFAULT_MAX_REFLECTION_LOD = 4.0;  // Fallback if push constant is 0

// Shadow constants (from HelloVulkan)
const float SHADOW_AMBIENT = 0.25;  // Shadows are not completely black (increased)
const int PCF_RANGE = 2;            // 5x5 kernel for softer shadows
const float PCF_SCALE = 1.0;

// Exposure and tonemapping
const float EXPOSURE = 1.0;         // Neutral exposure

// Poisson disk for shadow sampling (from HelloVulkan)
const int POISSON_COUNT = 16;
const vec2 poissonDisk[POISSON_COUNT] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845),
    vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507),
    vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790)
);

// ============================================================================
// PBR Functions (from HelloVulkan)
// ============================================================================

// Specular D - Trowbridge-Reitz GGX
float DistributionGGX(float NoH, float roughness) {
    float alpha = roughness * roughness;  // Disney remapping
    float alpha2 = alpha * alpha;
    float NoH2 = NoH * NoH;

    float nominator = alpha2;
    float denominator = (NoH2 * (alpha2 - 1.0) + 1.0);
    denominator = PI * denominator * denominator;

    return nominator / denominator;
}

// Roughness remapping for direct lighting (Brian Karis's PBR Note)
float AlphaDirectLighting(float roughness) {
    float r = (roughness + 1.0);
    return (r * r) / 8.0;
}

// Specular G - Geometry function (microfacet self-shadowing)
float GeometrySchlickGGX(float NoL, float NoV, float alpha) {
    float GL = NoL / (NoL * (1.0 - alpha) + alpha);
    float GV = NoV / (NoV * (1.0 - alpha) + alpha);
    return GL * GV;
}

// Specular F - Fresnel-Schlick
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Basic Lambertian diffuse
vec3 Diffuse(vec3 albedo) {
    return albedo / PI;
}

// ============================================================================
// Tonemapping (from HelloVulkan)
// ============================================================================

// ACES Filmic Tone Mapping (looks much better than Reinhard)
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard with luminance preservation
vec3 ReinhardLuminance(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float mappedLuminance = luminance / (1.0 + luminance);
    return (mappedLuminance / max(luminance, 0.0001)) * color;
}

// Gamma correction
vec3 GammaCorrect(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

// ============================================================================
// Normal Mapping
// ============================================================================

vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent.xyz);
    T = normalize(T - dot(T, N) * N);  // Gram-Schmidt orthogonalization
    vec3 B = cross(N, T) * fragTangent.w;
    
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

// ============================================================================
// Shadow Mapping (from HelloVulkan)
// ============================================================================

// Pseudo random number for Poisson disk rotation
float Rand(vec3 seed, int i) {
    vec4 seed4 = vec4(seed, float(i));
    float dotValue = dot(seed4, vec4(12.9898, 78.233, 45.164, 94.673));
    return fract(sin(dotValue) * 43758.5453);
}

// Get rotated Poisson disk coordinate
vec2 GetPoissonDiskCoord(vec2 projCoords, int i, float radius) {
    int index = int(float(POISSON_COUNT) * Rand(projCoords.xyy, i)) % POISSON_COUNT;
    return projCoords.xy + poissonDisk[index] / radius;
}

// Calculate shadow with Poisson disk soft shadows
float calculateShadowPoisson(vec3 worldPos, vec3 N, vec3 L) {
    if (shadowEffects.shadowEnabled == 0) {
        return 1.0;  // No shadow (fully lit)
    }

    // Transform world position to light space
    vec4 lightSpacePos = shadowEffects.lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    // Transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    // Return fully lit if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }
    
    // Slope-based bias (from HelloVulkan)
    float NoL = max(dot(N, L), 0.0);
    float bias = max(shadowEffects.shadowParams.y * (1.0 - NoL), shadowEffects.shadowParams.x);
    
    // Poisson disk soft shadows
    ivec2 texDim = textureSize(shadowMap, 0).xy;
    float dx = 1.0 / float(texDim.x);
    float dy = 1.0 / float(texDim.y);
    float poissonRadius = 3000.0;  // Smaller = blurrier
    
    float shadow = 0.0;
    int count = 0;
    
    for (int x = -PCF_RANGE; x <= PCF_RANGE; x++) {
        for (int y = -PCF_RANGE; y <= PCF_RANGE; y++) {
            vec2 off = vec2(dx * float(x), dy * float(y));
            vec2 coord = GetPoissonDiskCoord(projCoords.xy + off, count, poissonRadius);
            
            // Use hardware shadow comparison
            float shadowSample = texture(shadowMap, vec3(coord, projCoords.z - bias));
            shadow += mix(SHADOW_AMBIENT, 1.0, shadowSample);
            count++;
        }
    }
    
    return shadow / float(count);
}

// ============================================================================
// IBL Ambient Calculation (from HelloVulkan)
// ============================================================================

vec3 calculateAmbient(vec3 albedo, vec3 F0, vec3 N, vec3 V, 
                      float metallic, float roughness, float ao, float NoV) {
    vec3 F = FresnelSchlickRoughness(NoV, F0, roughness);
    
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    
    // Sample IBL maps
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 R = reflect(-V, N);
    // Use push constant maxReflectionLod for IBL sampling (from HelloVulkan)
    float reflectionLod = pc.maxReflectionLod > 0.0 ? pc.maxReflectionLod : DEFAULT_MAX_REFLECTION_LOD;
    vec3 prefilteredColor = textureLod(prefilteredMap, R, roughness * reflectionLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
    
    // Check if IBL maps are valid (not just gray fallback)
    // Fallback to hemisphere lighting if IBL is too uniform
    float iblIntensity = max(max(irradiance.r, irradiance.g), irradiance.b);
    
    vec3 diffuse;
    vec3 specular;
    
    if (iblIntensity < 0.01) {
        // IBL maps are probably fallback gray - use hemisphere lighting
        vec3 skyColor = vec3(0.5, 0.6, 0.8);  // Sky blue
        vec3 groundColor = vec3(0.2, 0.15, 0.1);  // Earth brown
        float hemisphere = N.y * 0.5 + 0.5;  // 0 = ground, 1 = sky
        vec3 hemisphereLight = mix(groundColor, skyColor, hemisphere) * 0.15;  // Reduced from 0.5
        
        diffuse = hemisphereLight * albedo;
        
        // Simple environment reflection based on view angle
        float reflectivity = (1.0 - roughness) * (1.0 - roughness);
        specular = mix(groundColor, skyColor, R.y * 0.5 + 0.5) * reflectivity * F * 0.1;  // Reduced from 0.3
    } else {
        // Normal IBL path
        diffuse = irradiance * albedo;
        specular = prefilteredColor * (F * brdf.x + brdf.y);
    }
    
    return (kD * diffuse + specular) * ao;
}

// ============================================================================
// Direct Light Radiance (from HelloVulkan)
// ============================================================================

vec3 calculateRadiance(vec3 albedo, vec3 N, vec3 V, vec3 F0,
                       float metallic, float roughness, float alphaRoughness,
                       float NoV, vec3 lightPos, vec3 lightColor, float lightIntensity,
                       float lightType) {
    vec3 L;
    float attenuation = 1.0;
    
    if (lightType == 0.0) {
        // Directional light - lightPos is actually direction
        L = normalize(lightPos);
    } else {
        // Point light
        vec3 lightDir = lightPos - fragWorldPos;
        float distance = length(lightDir);
        L = lightDir / distance;
        
        // Physically correct attenuation with configurable falloff (from HelloVulkan)
        // pc.lightFalloff: lower = slower falloff, higher = faster falloff
        float falloff = pc.lightFalloff > 0.0 ? pc.lightFalloff : 2.0;
        attenuation = 1.0 / pow(max(distance, 0.1), falloff);
    }
    
    vec3 H = normalize(V + L);
    float NoH = max(dot(N, H), 0.0);
    float NoL = max(dot(N, L), 0.0);
    float HoV = max(dot(H, V), 0.0);
    
    vec3 radiance = lightColor * attenuation * lightIntensity;
    
    // Cook-Torrance BRDF
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
    
    return (diffuse + specular) * radiance * NoL;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // Check for debug modes first
    if ((pc.flags & FLAG_DEBUG_NORMALS) != 0u) {
        vec3 N = getNormalFromMap();
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    
    // Debug: show UVs
    if ((pc.flags & FLAG_DEBUG_UV) != 0u) {
        outColor = vec4(fragTexCoord, 0.0, 1.0);
        return;
    }
    
    // Sample albedo texture and manually convert sRGB to linear (like HelloVulkan)
    vec4 rawAlbedo = texture(albedoMap, fragTexCoord);
    
    // Alpha cutoff FIRST, before any processing (like HelloVulkan)
    // Use raw texture alpha with simple 0.5 threshold
    if (rawAlbedo.a < 0.5) {
        discard;
    }
    
    // Now process colors
    vec3 albedo = pow(rawAlbedo.rgb, vec3(2.2)) * material.baseColorFactor.rgb;
    float alpha = rawAlbedo.a;
    
    // Debug: show raw texture
    if ((pc.flags & FLAG_DEBUG_ALBEDO) != 0u) {
        outColor = rawAlbedo;  // Show raw texture as stored
        return;
    }
    
    // Debug 9: show actual albedo used in lighting (after baseColorFactor)
    if ((pc.flags & (1u << 16)) != 0u) {
        outColor = vec4(albedo, 1.0);  // Show albedo * baseColorFactor
        return;
    }
    
    // Debug: show baseColorFactor itself (press key to add)
    if ((pc.flags & (1u << 17)) != 0u) {
        outColor = material.baseColorFactor;
        return;
    }
    
    // Metallic-roughness
    vec3 mrSample = texture(metallicRoughnessMap, fragTexCoord).rgb;
    float roughness = clamp(mrSample.g * material.roughnessFactor, MIN_ROUGHNESS, 1.0);
    float metallic = mrSample.b * material.metallicFactor;
    
    // Apply overrides from push constants if enabled
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
    
    float alphaRoughness = AlphaDirectLighting(roughness);
    
    // Other material properties
    float ao = texture(occlusionMap, fragTexCoord).r;
    // Emissive - convert sRGB to linear like albedo
    vec3 emissive = pow(texture(emissiveMap, fragTexCoord).rgb, vec3(2.2));
    
    // Debug: show emissive
    if ((pc.flags & FLAG_DEBUG_EMISSIVE) != 0u) {
        outColor = vec4(emissive, 1.0);
        return;
    }
    
    if ((pc.flags & FLAG_DEBUG_AO) != 0u) {
        outColor = vec4(vec3(ao), 1.0);
        return;
    }
    
    // Normals and view
    vec3 N;
    if ((pc.flags & FLAG_DISABLE_NORMAL_MAPPING) != 0u) {
        N = normalize(fragNormal);
    } else {
        N = getNormalFromMap();
    }
    vec3 V = normalize(camera.viewPos - fragWorldPos);
    float NoV = max(dot(N, V), 0.0);
    
    // F0 - reflectance at normal incidence (use push constant base reflectivity)
    vec3 F0 = vec3(pc.baseReflectivity);
    F0 = mix(F0, albedo, metallic);
    
    // Debug: show F0 (should be colored for metals, gray for dielectrics)
    if ((pc.flags & FLAG_DEBUG_F0) != 0u) {
        outColor = vec4(F0, 1.0);
        return;
    }
    
    // Accumulate direct lighting
    // Add albedoMultiplier to brighten dark scenes (from HelloVulkan)
    vec3 Lo = albedo * pc.albedoMultiplier;
    vec3 diffuseAccum = vec3(0.0);  // For debug
    vec3 specularAccum = vec3(0.0); // For debug
    bool shadowApplied = false;
    
    if ((pc.flags & FLAG_DISABLE_DIRECT_LIGHT) == 0u) {
        for (int i = 0; i < lights.numLights && i < 16; i++) {
            vec3 lightPos = lights.lightPositions[i].xyz;
            float lightType = lights.lightPositions[i].w;
            vec3 lightColor = lights.lightColors[i].xyz;
            float lightIntensity = lights.lightColors[i].w;
            
            // Manually calculate for debug separation
            vec3 L;
            float attenuation = 1.0;
            if (lightType == 0.0) {
                L = normalize(lightPos);
            } else {
                vec3 lightDir = lightPos - fragWorldPos;
                float distance = length(lightDir);
                L = lightDir / distance;
                float falloff = pc.lightFalloff > 0.0 ? pc.lightFalloff : 2.0;
                attenuation = 1.0 / pow(max(distance, 0.1), falloff);
            }
            
            vec3 H = normalize(V + L);
            float NoH = max(dot(N, H), 0.0);
            float NoL = max(dot(N, L), 0.0);
            float HoV = max(dot(H, V), 0.0);
            
            vec3 radiance = lightColor * attenuation * lightIntensity;
            
            // Cook-Torrance BRDF components
            float D = DistributionGGX(NoH, roughness);
            float G = GeometrySchlickGGX(NoL, NoV, alphaRoughness);
            vec3 F = FresnelSchlick(HoV, F0);
            
            vec3 specular = (D * G * F) / (4.0 * NoV * NoL + 0.0001);
            
            vec3 kS = F;
            vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
            vec3 diffuse = Diffuse(kD * albedo);
            
            // Apply shadow only to first directional light
            float shadow = 1.0;
            if (lightType == 0.0 && !shadowApplied) {
                vec3 Ldir = normalize(lightPos);
                float rawShadow = calculateShadowPoisson(fragWorldPos, N, Ldir);
                shadow = mix(1.0, rawShadow, pc.shadowIntensity);
                shadowApplied = true;
            }
            
            diffuseAccum += diffuse * radiance * NoL * shadow;
            specularAccum += specular * radiance * NoL * shadow;
            Lo += (diffuse + specular) * radiance * NoL * shadow;
        }
    }
    
    // Apply direct light intensity from push constants
    Lo *= pc.directLightIntensity;
    diffuseAccum *= pc.directLightIntensity;
    specularAccum *= pc.directLightIntensity;
    
    // Debug: show diffuse contribution only
    if ((pc.flags & FLAG_DEBUG_DIFFUSE_ONLY) != 0u) {
        outColor = vec4(diffuseAccum, 1.0);
        return;
    }
    
    // Debug: show specular contribution only
    if ((pc.flags & FLAG_DEBUG_SPECULAR_ONLY) != 0u) {
        outColor = vec4(specularAccum, 1.0);
        return;
    }
    
    // IBL ambient (skip if disabled via push constants)
    vec3 ambient = vec3(0.0);
    if ((pc.flags & FLAG_DISABLE_IBL) == 0u) {
        ambient = calculateAmbient(albedo, F0, N, V, metallic, roughness, ao, NoV);
    }
    
    // Add basic ambient light fallback
    vec3 ambientLight = lights.ambientColor.rgb * lights.ambientColor.w;
    ambient += albedo * ambientLight * ao * 0.1;
    
    // Apply ambient intensity from push constants
    ambient *= pc.ambientIntensity;
    
    // Apply emissive intensity from push constants
    vec3 emissiveContrib = emissive * pc.emissiveIntensity;
    
    // Final color with exposure
    vec3 color = (ambient + Lo + emissiveContrib) * pc.exposure;
    
    // Tonemapping (can be toggled off for debugging)
    if ((pc.flags & FLAG_DISABLE_TONEMAPPING) == 0u) {
        color = color / (color + vec3(1.0));  // Reinhard
    }
    
    // The swapchain uses VK_FORMAT_B8G8R8A8_SRGB, which automatically 
    // converts linear color to sRGB gamma on output.
    
    outColor = vec4(color, alpha);
}
