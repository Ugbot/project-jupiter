// PBR BRDF Functions
// Ported from Sascha Willems' vulkan-gltf-pbr (MIT License)
// Based on Khronos WebGL PBR reference implementation

const float PI = 3.141592653589793;
const float MIN_ROUGHNESS = 0.04;

// Encapsulate PBR inputs for cleaner function signatures
struct PBRInfo {
    float NdotL;                  // cos angle between normal and light direction
    float NdotV;                  // cos angle between normal and view direction
    float NdotH;                  // cos angle between normal and half vector
    float LdotH;                  // cos angle between light direction and half vector
    float VdotH;                  // cos angle between view direction and half vector
    float perceptualRoughness;    // roughness as authored
    float metalness;              // metallic value at the surface
    vec3 reflectance0;            // full reflectance color (normal incidence)
    vec3 reflectance90;           // reflectance color at grazing angle
    float alphaRoughness;         // roughness squared (more linear response)
    vec3 diffuseColor;            // color contribution from diffuse lighting
    vec3 specularColor;           // color contribution from specular lighting
};

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria
vec3 diffuse(PBRInfo pbrInputs) {
    return pbrInputs.diffuseColor / PI;
}

// Fresnel reflectance term (Schlick approximation)
// F(v,h) = F0 + (F90 - F0) * (1 - VdotH)^5
vec3 specularReflection(PBRInfo pbrInputs) {
    return pbrInputs.reflectance0 +
           (pbrInputs.reflectance90 - pbrInputs.reflectance0) *
           pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// Geometric attenuation (Smith's method)
// G(l,v,h) - rougher materials reflect less light back to viewer
float geometricOcclusion(PBRInfo pbrInputs) {
    float NdotL = pbrInputs.NdotL;
    float NdotV = pbrInputs.NdotV;
    float r = pbrInputs.alphaRoughness;

    float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
    float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
    return attenuationL * attenuationV;
}

// Microfacet distribution (GGX/Trowbridge-Reitz)
// D(h) - distribution of microfacet normals
float microfacetDistribution(PBRInfo pbrInputs) {
    float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
    float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
    return roughnessSq / (PI * f * f);
}
