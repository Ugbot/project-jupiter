/**
 * @file pbr_functions.glsl
 * @brief Core PBR functions (from HelloVulkan)
 * 
 * Contains Cook-Torrance BRDF building blocks:
 * - Distribution function (GGX/Trowbridge-Reitz)
 * - Geometry function (Smith's method)
 * - Fresnel function (Schlick approximation)
 * 
 * Notations:
 *   V - View unit vector
 *   L - Incident light unit vector
 *   N - Surface normal unit vector
 *   H - Half unit vector between L and V
 * 
 * Usage: #include <includes/pbr_functions.glsl>
 */

#ifndef PBR_FUNCTIONS_GLSL
#define PBR_FUNCTIONS_GLSL

#include "constants.glsl"

// =============================================================================
// Specular D - Distribution Function
// =============================================================================

/**
 * @brief Trowbridge-Reitz GGX normal distribution function
 * 
 * Models the distribution of microfacet normals on the surface.
 * 
 * @param NoH Dot product of Normal and Halfway vector
 * @param roughness Material roughness (0-1)
 * @return Distribution value
 */
float DistributionGGX(float NoH, float roughness) {
    float alpha = roughness * roughness;  // Disney remapping
    float alpha2 = alpha * alpha;
    float NoH2 = NoH * NoH;

    float nominator = alpha2;
    float denominator = (NoH2 * (alpha2 - 1.0) + 1.0);
    denominator = PI * denominator * denominator;

    return nominator / denominator;
}

// =============================================================================
// Specular G - Geometry Function
// =============================================================================

/**
 * @brief Roughness remapping for direct lighting
 * 
 * From Brian Karis's PBR Note - different alpha for direct vs IBL lighting.
 * 
 * @param roughness Material roughness
 * @return Remapped alpha for geometry term
 */
float AlphaDirectLighting(float roughness) {
    float r = (roughness + 1.0);
    return (r * r) / 8.0;
}

/**
 * @brief Roughness remapping for IBL lighting
 * 
 * @param roughness Material roughness
 * @return Remapped alpha for geometry term
 */
float AlphaIBLLighting(float roughness) {
    float alpha = roughness * roughness;
    return (alpha * alpha) / 2.0;
}

/**
 * @brief Geometry function using Smith's method with GGX
 * 
 * Describes self-shadowing of microfacets.
 * 
 * @param NoL Dot product of Normal and Light direction
 * @param NoV Dot product of Normal and View direction
 * @param alpha Remapped roughness (use AlphaDirectLighting or AlphaIBLLighting)
 * @return Geometry attenuation factor
 */
float GeometrySchlickGGX(float NoL, float NoV, float alpha) {
    float GL = NoL / (NoL * (1.0 - alpha) + alpha);
    float GV = NoV / (NoV * (1.0 - alpha) + alpha);
    return GL * GV;
}

// =============================================================================
// Specular F - Fresnel Function
// =============================================================================

/**
 * @brief Fresnel-Schlick approximation
 * 
 * Describes the ratio of reflected vs refracted light at different angles.
 * 
 * @param cosTheta Cosine of angle between view and halfway vector
 * @param F0 Reflectance at normal incidence (base reflectivity)
 * @return Fresnel reflectance
 */
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/**
 * @brief Fresnel-Schlick with roughness term for IBL
 * 
 * Modified version that accounts for roughness in ambient lighting.
 * 
 * @param cosTheta Cosine of angle between view and normal
 * @param F0 Reflectance at normal incidence
 * @param roughness Material roughness
 * @return Fresnel reflectance
 */
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =============================================================================
// Diffuse
// =============================================================================

/**
 * @brief Basic Lambertian diffuse BRDF
 * 
 * @param albedo Surface color
 * @return Diffuse contribution
 */
vec3 Diffuse(vec3 albedo) {
    return albedo / PI;
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Calculate F0 from IOR (Index of Refraction)
 * 
 * @param ior Index of refraction (1.5 for most dielectrics)
 * @return Base reflectivity at normal incidence
 */
float F0FromIOR(float ior) {
    float f0 = (ior - 1.0) / (ior + 1.0);
    return f0 * f0;
}

/**
 * @brief Mix F0 between dielectric and metallic
 * 
 * @param albedo Surface color
 * @param metallic Metallic factor (0-1)
 * @param dielectricF0 F0 for dielectric surfaces (default 0.04)
 * @return Final F0 value
 */
vec3 CalculateF0(vec3 albedo, float metallic, float dielectricF0) {
    return mix(vec3(dielectricF0), albedo, metallic);
}

#endif // PBR_FUNCTIONS_GLSL

