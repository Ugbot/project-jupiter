/**
 * @file material_ubo.glsl
 * @brief Material properties uniform buffer
 * 
 * Defines material UBO for PBR rendering.
 * 
 * Usage: #include <includes/material_ubo.glsl>
 */

#ifndef MATERIAL_UBO_GLSL
#define MATERIAL_UBO_GLSL

/**
 * @brief Alpha blend modes
 */
#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK   1
#define ALPHA_MODE_BLEND  2

/**
 * @brief Material properties uniform buffer
 * 
 * Contains PBR material factors that multiply with texture values.
 * Follows glTF 2.0 material model.
 */
struct MaterialUBO {
    vec4 baseColorFactor;     // Base color multiplier (RGBA)
    vec3 emissiveFactor;      // Emissive color multiplier
    float metallicFactor;     // Metallic multiplier
    float roughnessFactor;    // Roughness multiplier
    float alphaCutoff;        // Alpha test threshold (for ALPHA_MODE_MASK)
    uint alphaMode;           // ALPHA_MODE_OPAQUE, MASK, or BLEND
    uint doubleSided;         // Whether material is double-sided
};

/**
 * @brief Extended material properties for advanced rendering
 * 
 * Additional properties for subsurface scattering, clearcoat, etc.
 */
struct MaterialExtendedUBO {
    // Base material
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float alphaCutoff;
    uint alphaMode;
    uint doubleSided;
    
    // Clearcoat extension
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    
    // Transmission extension
    float transmissionFactor;
    float ior;
    
    // Sheen extension
    vec3 sheenColorFactor;
    float sheenRoughnessFactor;
    
    // Specular extension
    float specularFactor;
    vec3 specularColorFactor;
    
    float _padding0;
};

#endif // MATERIAL_UBO_GLSL

