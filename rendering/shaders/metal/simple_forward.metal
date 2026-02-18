/**
 * @file simple_forward.metal
 * @brief Simple forward renderer shader (Lambertian lighting)
 * 
 * Basic forward renderer for Metal backend.
 * Supports:
 * - Vertex transformation (MVP)
 * - Lambertian diffuse lighting
 * - Texture sampling
 * - Simple materials
 */

#include <metal_stdlib>
using namespace metal;

// ============================================================================
// Structures
// ============================================================================

// Vertex input
struct Vertex {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
};

// Uniforms (Set 0, Binding 0)
struct CameraUniforms {
    float4x4 view;
    float4x4 projection;
};

// Lighting (Set 0, Binding 1)
struct LightingUniforms {
    float4 sunDirIntensity;  // xyz = direction, w = intensity
    float4 sunColor;
    float4 ambientColor;     // rgb = color, a = intensity
};

// Material (Set 1, Binding 0)
struct MaterialUniforms {
    float4 baseColor;
    float metallic;
    float roughness;
    float pad0;
    float pad1;
};

// Per-object (push constant)
struct ObjectUniforms {
    float4x4 model;
};

// Vertex output / Fragment input
struct Varyings {
    float4 position [[position]];
    float3 worldPosition;
    float3 normal;
    float2 texCoord;
};

// ============================================================================
// Vertex Shader
// ============================================================================

vertex Varyings vertexMain(
    Vertex in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant ObjectUniforms& object [[buffer(1)]]
) {
    Varyings out;
    
    // Transform to world space
    float4 worldPos = object.model * float4(in.position, 1.0);
    out.worldPosition = worldPos.xyz;
    
    // Transform normal to world space
    out.normal = (object.model * float4(in.normal, 0.0)).xyz;
    
    // Transform to clip space
    out.position = camera.projection * camera.view * worldPos;
    
    // Pass through texture coordinates
    out.texCoord = in.texCoord;
    
    return out;
}

// ============================================================================
// Fragment Shader
// ============================================================================

fragment float4 fragmentMain(
    Varyings in [[stage_in]],
    constant LightingUniforms& lighting [[buffer(0)]],
    constant MaterialUniforms& material [[buffer(1)]],
    texture2d<float> albedoTexture [[texture(0)]],
    sampler albedoSampler [[sampler(0)]]
) {
    // Sample albedo texture
    float4 albedo = albedoTexture.sample(albedoSampler, in.texCoord);
    albedo *= material.baseColor;
    
    // Normalize normal
    float3 N = normalize(in.normal);
    
    // Light direction (pointing FROM surface TO light)
    float3 L = normalize(-lighting.sunDirIntensity.xyz);
    
    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = albedo.rgb * lighting.sunColor.rgb * NdotL * lighting.sunDirIntensity.w;
    
    // Ambient
    float3 ambient = albedo.rgb * lighting.ambientColor.rgb * lighting.ambientColor.a;
    
    // Final color
    float3 color = diffuse + ambient;
    
    return float4(color, albedo.a);
}

