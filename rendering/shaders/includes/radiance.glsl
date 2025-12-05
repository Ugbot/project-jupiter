/**
 * @file radiance.glsl
 * @brief Direct light radiance calculation (from HelloVulkan)
 * 
 * Calculates the outgoing radiance from a single light source using
 * the Cook-Torrance BRDF.
 * 
 * Usage: #include <includes/radiance.glsl>
 * Requires: pbr_functions.glsl
 */

#ifndef RADIANCE_GLSL
#define RADIANCE_GLSL

#include "pbr_functions.glsl"

/**
 * @brief Light types for PBR calculation
 */
#define LIGHT_TYPE_DIRECTIONAL 0.0
#define LIGHT_TYPE_POINT       1.0
#define LIGHT_TYPE_SPOT        2.0

/**
 * @brief Calculate radiance from a single light source
 * 
 * @param worldPos Fragment world position
 * @param albedo Surface color (linear)
 * @param N Surface normal
 * @param V View direction
 * @param F0 Base reflectivity
 * @param metallic Metallic factor
 * @param roughness Roughness factor
 * @param alphaRoughness Remapped roughness for direct lighting
 * @param NoV Dot product of N and V
 * @param lightPos Light position (or direction for directional)
 * @param lightColor Light color
 * @param lightIntensity Light intensity
 * @param lightType LIGHT_TYPE_DIRECTIONAL, LIGHT_TYPE_POINT, or LIGHT_TYPE_SPOT
 * @param lightFalloff Attenuation power (default 2.0 for inverse square)
 * @return Outgoing radiance from this light
 */
vec3 calculateRadiance(
    vec3 worldPos,
    vec3 albedo,
    vec3 N,
    vec3 V,
    vec3 F0,
    float metallic,
    float roughness,
    float alphaRoughness,
    float NoV,
    vec3 lightPos,
    vec3 lightColor,
    float lightIntensity,
    float lightType,
    float lightFalloff
) {
    vec3 L;
    float attenuation = 1.0;
    
    if (lightType == LIGHT_TYPE_DIRECTIONAL) {
        // Directional light - lightPos is actually direction
        L = normalize(lightPos);
    } else {
        // Point light (or spot - cone handled elsewhere)
        vec3 lightDir = lightPos - worldPos;
        float distance = length(lightDir);
        L = lightDir / distance;
        
        // Physically correct attenuation with minimum distance
        attenuation = 1.0 / pow(max(distance, 0.1), lightFalloff);
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
    
    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3 diffuse = Diffuse(kD * albedo);
    
    return (diffuse + specular) * radiance * NoL;
}

/**
 * @brief Simplified radiance calculation with default falloff
 */
vec3 calculateRadiance(
    vec3 worldPos,
    vec3 albedo,
    vec3 N,
    vec3 V,
    vec3 F0,
    float metallic,
    float roughness,
    float alphaRoughness,
    float NoV,
    vec3 lightPos,
    vec3 lightColor,
    float lightIntensity,
    float lightType
) {
    return calculateRadiance(
        worldPos, albedo, N, V, F0, metallic, roughness,
        alphaRoughness, NoV, lightPos, lightColor, lightIntensity, lightType, 2.0
    );
}

#endif // RADIANCE_GLSL

