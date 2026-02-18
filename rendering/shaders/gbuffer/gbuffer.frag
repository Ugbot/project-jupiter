#version 460 core
#extension GL_GOOGLE_include_directive : require

/**
 * G-buffer geometry pass - fragment shader (Deferred Rendering)
 * 
 * Outputs all material properties to G-buffer textures:
 * - Position: view-space XYZ + foreground flag (W)
 * - Normal: view-space normal mapped normal
 * - Albedo + Metallic
 * - Roughness + AO
 * - Emissive
 */

// Shared utilities (GammaToLinear, etc.)
#include "../includes/tonemap.glsl"
// Material factors (baseColorFactor, emissiveFactor, metallicFactor, roughnessFactor, etc.)
#include "../includes/material_ubo.glsl"

// Inputs from vertex shader
layout(location = 0) in vec3 inViewPos;
layout(location = 1) in vec3 inViewNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inViewTangent;
layout(location = 4) in vec3 inWorldPos;

// MRT outputs (5 color attachments)
layout(location = 0) out vec4 outPosition;   // XYZ = view-space position, W = foreground flag
layout(location = 1) out vec4 outNormal;     // RGB = view-space normal
layout(location = 2) out vec4 outAlbedo;     // RGB = albedo, A = metallic
layout(location = 3) out vec4 outMaterial;   // R = roughness, G = AO, B = reserved, A = reserved
layout(location = 4) out vec4 outEmissive;   // RGB = emissive, A = unused

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

// Material textures (Set 1)
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = 1, binding = 3) uniform sampler2D occlusionTexture;
layout(set = 1, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 1, binding = 5) uniform MaterialProperties {
    MaterialUBO material;
} mat;

/**
 * Calculate TBN matrix and transform normal from tangent space to view space
 */
vec3 getNormalFromMap(vec3 geometricNormal, vec4 tangent, vec2 texCoord) {
    vec3 tangentNormal = texture(normalTexture, texCoord).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(geometricNormal);
    vec3 T = normalize(tangent.xyz);
    T = normalize(T - dot(T, N) * N);  // Gram-Schmidt orthogonalization
    vec3 B = cross(N, T) * tangent.w;  // Handedness from tangent.w
    
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main() {
    // Sample material textures
    vec4 baseColor = texture(baseColorTexture, inTexCoord);
    vec4 metallicRoughness = texture(metallicRoughnessTexture, inTexCoord);
    float occlusion = texture(occlusionTexture, inTexCoord).r;
    vec3 emissive = texture(emissiveTexture, inTexCoord).rgb;
    
    // Extract material properties (glTF convention: G = roughness, B = metallic)
    float metallic = metallicRoughness.b * mat.material.metallicFactor;
    float roughness = metallicRoughness.g * mat.material.roughnessFactor;
    
    // Calculate normal-mapped normal in view space
    vec3 normal = getNormalFromMap(inViewNormal, inViewTangent, inTexCoord);
    
    // Output to G-buffer attachments
    // Position: view-space position, W = 0.0 for foreground geometry
    outPosition = vec4(inViewPos, 0.0);
    
    // Normal: view-space normal mapped normal
    outNormal = vec4(normal, 1.0);
    
    // Albedo + Metallic
    // NOTE: Asset textures are UNORM even for sRGB; convert to linear before storing.
    vec3 baseColorLinear = GammaToLinear(baseColor.rgb) * mat.material.baseColorFactor.rgb;
    outAlbedo = vec4(baseColorLinear, clamp(metallic, 0.0, 1.0));
    
    // Material: Roughness + AO
    outMaterial = vec4(clamp(roughness, 0.0, 1.0), clamp(occlusion, 0.0, 1.0), 0.0, 0.0);
    
    // Emissive
    vec3 emissiveLinear = GammaToLinear(emissive) * mat.material.emissiveFactor;
    outEmissive = vec4(emissiveLinear, 0.0);
}
