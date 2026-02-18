/**
 * @file ambient.glsl
 * @brief IBL ambient lighting calculation (from HelloVulkan)
 * 
 * Calculates ambient lighting using Image-Based Lighting with the
 * split-sum approximation for PBR.
 * 
 * Usage: #include <includes/ambient.glsl>
 * Requires: pbr_functions.glsl, constants.glsl
 */

#ifndef AMBIENT_GLSL
#define AMBIENT_GLSL

#include "pbr_functions.glsl"

/**
 * @brief Calculate ambient lighting using IBL
 * 
 * Uses the split-sum approximation:
 * - Diffuse: Irradiance map convolved with cosine lobe
 * - Specular: Pre-filtered environment map + BRDF LUT
 * 
 * @param irradianceMap Diffuse irradiance cubemap
 * @param prefilteredMap Pre-filtered specular cubemap
 * @param brdfLUT BRDF integration lookup texture
 * @param albedo Surface color (linear)
 * @param F0 Base reflectivity
 * @param N Surface normal
 * @param V View direction
 * @param metallic Metallic factor
 * @param roughness Roughness factor
 * @param ao Ambient occlusion
 * @param NoV Dot product of N and V
 * @param maxReflectionLod Maximum mip level for specular
 * @return Ambient contribution
 */
vec3 calculateAmbientIBL(
    samplerCube irradianceMap,
    samplerCube prefilteredMap,
    sampler2D brdfLUT,
    vec3 albedo,
    vec3 F0,
    vec3 N,
    vec3 V,
    float metallic,
    float roughness,
    float ao,
    float NoV,
    float maxReflectionLod
) {
    vec3 F = FresnelSchlickRoughness(NoV, F0, roughness);
    
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    
    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;
    
    // Specular IBL - Split-Sum approximation
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureLod(prefilteredMap, R, roughness * maxReflectionLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);
    
    return (kD * diffuse + specular) * ao;
}

/**
 * @brief Hemisphere lighting fallback when IBL maps are not available
 * 
 * Simple sky/ground gradient for ambient lighting.
 * 
 * @param albedo Surface color (linear)
 * @param F0 Base reflectivity
 * @param N Surface normal
 * @param V View direction
 * @param metallic Metallic factor
 * @param roughness Roughness factor
 * @param ao Ambient occlusion
 * @param NoV Dot product of N and V
 * @param skyColor Color of sky (default blue)
 * @param groundColor Color of ground (default brown)
 * @param intensity Overall intensity multiplier
 * @return Ambient contribution
 */
vec3 calculateAmbientHemisphere(
    vec3 albedo,
    vec3 F0,
    vec3 N,
    vec3 V,
    float metallic,
    float roughness,
    float ao,
    float NoV,
    vec3 skyColor,
    vec3 groundColor,
    float intensity
) {
    vec3 F = FresnelSchlickRoughness(NoV, F0, roughness);
    
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    
    // Hemisphere gradient based on normal Y component
    float hemisphere = N.y * 0.5 + 0.5;  // 0 = ground, 1 = sky
    vec3 hemisphereLight = mix(groundColor, skyColor, hemisphere) * intensity;
    
    vec3 diffuse = hemisphereLight * albedo;
    
    // Simple environment reflection
    vec3 R = reflect(-V, N);
    float reflectivity = (1.0 - roughness) * (1.0 - roughness);
    vec3 specular = mix(groundColor, skyColor, R.y * 0.5 + 0.5) * reflectivity * F * (intensity * 0.5);
    
    return (kD * diffuse + specular) * ao;
}

/**
 * @brief Combined ambient with automatic IBL fallback
 * 
 * Checks if IBL maps are valid (not gray fallback) and switches
 * to hemisphere lighting if needed.
 * 
 * @param irradianceMap Diffuse irradiance cubemap
 * @param prefilteredMap Pre-filtered specular cubemap
 * @param brdfLUT BRDF integration lookup texture
 * @param albedo Surface color (linear)
 * @param F0 Base reflectivity
 * @param N Surface normal
 * @param V View direction
 * @param metallic Metallic factor
 * @param roughness Roughness factor
 * @param ao Ambient occlusion
 * @param NoV Dot product of N and V
 * @param maxReflectionLod Maximum mip level for specular
 * @return Ambient contribution
 */
vec3 calculateAmbient(
    samplerCube irradianceMap,
    samplerCube prefilteredMap,
    sampler2D brdfLUT,
    vec3 albedo,
    vec3 F0,
    vec3 N,
    vec3 V,
    float metallic,
    float roughness,
    float ao,
    float NoV,
    float maxReflectionLod
) {
    // Sample irradiance to check if IBL is valid
    vec3 irradiance = texture(irradianceMap, N).rgb;
    float iblIntensity = max(max(irradiance.r, irradiance.g), irradiance.b);
    
    if (iblIntensity < 0.01) {
        // IBL maps are probably fallback gray - use hemisphere lighting
        vec3 skyColor = vec3(0.5, 0.6, 0.8);
        vec3 groundColor = vec3(0.2, 0.15, 0.1);
        return calculateAmbientHemisphere(
            albedo, F0, N, V, metallic, roughness, ao, NoV,
            skyColor, groundColor, 0.15
        );
    } else {
        // Normal IBL path
        return calculateAmbientIBL(
            irradianceMap, prefilteredMap, brdfLUT,
            albedo, F0, N, V, metallic, roughness, ao, NoV,
            maxReflectionLod
        );
    }
}

#endif // AMBIENT_GLSL









