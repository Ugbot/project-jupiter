/**
 * @file material_ubo.glsl
 * @brief Material properties uniform buffer (std140)
 *
 * Must match `jupiter::rendering::Material::MaterialUBO` in C++ exactly.
 */

#ifndef MATERIAL_UBO_GLSL
#define MATERIAL_UBO_GLSL

// Alpha modes (glTF)
#define ALPHA_MODE_OPAQUE 0u
#define ALPHA_MODE_MASK   1u
#define ALPHA_MODE_BLEND  2u

// std140-aligned material UBO
struct MaterialUBO {
    vec4 baseColorFactor;     // RGBA

    // std140: vec3 takes 16 bytes, so w is padding
    vec3 emissiveFactor;      // RGB
    float metallicFactor;     // Metallic multiplier

    float roughnessFactor;    // Roughness multiplier
    float alphaCutoff;        // Alpha cutoff (MASK)
    uint alphaMode;           // 0/1/2
    uint doubleSided;         // 0/1

    // Texture presence flags (for shader-side decisions/debugging)
    uint hasBaseColorTex;
    uint hasNormalTex;
    uint hasMetallicRoughnessTex;
    uint hasOcclusionTex;
    uint hasEmissiveTex;
};

#endif // MATERIAL_UBO_GLSL







