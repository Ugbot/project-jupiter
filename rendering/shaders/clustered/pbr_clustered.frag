#version 460 core
#extension GL_GOOGLE_include_directive : require

/**
 * Clustered Forward PBR Fragment Shader
 * 
 * Uses clustered light data to efficiently shade surfaces with
 * potentially thousands of lights. Only lights affecting the
 * current cluster are evaluated.
 * 
 * Now uses Jupiter's unified shader include system.
 */

// Include unified shader libraries
#include "../includes/constants.glsl"
#include "../includes/pbr_functions.glsl"
#include "../includes/tonemap.glsl"

// ============================================================================
// Inputs
// ============================================================================

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// ============================================================================
// Outputs
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Data Structures
// ============================================================================

struct Light {
    vec4 position;  // xyz = world position, w = radius
    vec4 color;     // xyz = color, w = intensity
};

struct LightCell {
    uint offset;
    uint count;
};

// ============================================================================
// Uniforms
// ============================================================================

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;  // x=near, y=far, z=fov, w=aspect
} camera;

// Material UBO
layout(set = 0, binding = 1) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float emissiveFactor;
} material;

// Textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D occlusionMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

// Clustered light data
layout(set = 2, binding = 0) readonly buffer Lights {
    Light lights[];
};

layout(set = 2, binding = 1) readonly buffer LightCells {
    LightCell lightCells[];
};

layout(set = 2, binding = 2) readonly buffer LightIndices {
    uint lightIndices[];
};

// Push constants for cluster info + rendering parameters
layout(push_constant) uniform PushConstants {
    // Cluster configuration (16 bytes)
    uint clusterCountX;
    uint clusterCountY;
    uint clusterCountZ;
    uint screenWidth;
    
    // More cluster config (12 bytes)
    uint screenHeight;
    float zNear;
    float zFar;
    
    // Rendering parameters (16 bytes)
    float exposure;
    float ambientIntensity;
    float directLightIntensity;
    uint flags;
} pc;

// Flag bits for debug/feature toggles
const uint FLAG_DEBUG_CLUSTERS = 1u << 0;
const uint FLAG_DEBUG_NORMALS = 1u << 1;
const uint FLAG_DEBUG_LIGHTS = 1u << 2;
const uint FLAG_USE_ACES = 1u << 3;

// ============================================================================
// Cluster Computation
// ============================================================================

uint getClusterIndex(vec2 screenPos, float viewZ) {
    // Screen-space tile
    uint tileX = uint(screenPos.x / float(pc.screenWidth) * float(pc.clusterCountX));
    uint tileY = uint(screenPos.y / float(pc.screenHeight) * float(pc.clusterCountY));
    
    // Clamp to valid range
    tileX = min(tileX, pc.clusterCountX - 1);
    tileY = min(tileY, pc.clusterCountY - 1);
    
    // Depth slice (exponential distribution)
    float logZ = log(abs(viewZ) / pc.zNear) / log(pc.zFar / pc.zNear);
    uint tileZ = uint(logZ * float(pc.clusterCountZ));
    tileZ = min(tileZ, pc.clusterCountZ - 1);
    
    return tileX + tileY * pc.clusterCountX + tileZ * pc.clusterCountX * pc.clusterCountY;
}

// ============================================================================
// Normal Mapping
// ============================================================================

vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, inTexCoord).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // Sample textures
    vec4 albedoSample = texture(albedoMap, inTexCoord) * material.baseColorFactor;
    vec3 albedo = GammaToLinear(albedoSample.rgb);
    vec3 normal = getNormalFromMap();
    vec2 metallicRoughness = texture(metallicRoughnessMap, inTexCoord).bg;
    float metallic = metallicRoughness.x * material.metallicFactor;
    float roughness = max(metallicRoughness.y * material.roughnessFactor, MIN_ROUGHNESS);
    float ao = texture(occlusionMap, inTexCoord).r;
    vec3 emissive = GammaToLinear(texture(emissiveMap, inTexCoord).rgb) * material.emissiveFactor;

    // Calculate view direction
    vec3 V = normalize(camera.cameraPosition.xyz - inWorldPos);
    vec3 N = normal;

    // Debug visualizations
    if ((pc.flags & FLAG_DEBUG_NORMALS) != 0u) {
        outColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    // Calculate F0 using engine function
    vec3 F0 = CalculateF0(albedo, metallic, 0.04);

    // Get cluster for this fragment
    vec4 viewPos = camera.view * vec4(inWorldPos, 1.0);
    uint clusterIndex = getClusterIndex(gl_FragCoord.xy, viewPos.z);
    
    LightCell cell = lightCells[clusterIndex];

    // Debug cluster visualization
    if ((pc.flags & FLAG_DEBUG_CLUSTERS) != 0u) {
        float clusterHeat = float(cell.count) / 32.0;  // Normalize to expected max lights
        outColor = vec4(clusterHeat, 0.0, 1.0 - clusterHeat, 1.0);
        return;
    }

    // Debug light count visualization  
    if ((pc.flags & FLAG_DEBUG_LIGHTS) != 0u) {
        float lightHeat = float(cell.count) / 16.0;
        vec3 heatColor = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), lightHeat);
        outColor = vec4(heatColor, 1.0);
        return;
    }

    // Accumulate lighting
    vec3 Lo = vec3(0.0);
    float alphaRoughness = AlphaDirectLighting(roughness);

    // Process all lights in this cluster
    for (uint i = 0; i < cell.count; ++i) {
        uint lightIndex = lightIndices[cell.offset + i];
        Light light = lights[lightIndex];
        
        vec3 lightPos = light.position.xyz;
        float lightRadius = light.position.w;
        vec3 lightColor = light.color.rgb * light.color.w;
        
        // Calculate light vector and attenuation
        vec3 L = lightPos - inWorldPos;
        float distance = length(L);
        L = normalize(L);
        
        // Smooth attenuation with radius falloff
        float attenuation = 1.0 - smoothstep(0.0, lightRadius, distance);
        attenuation *= attenuation;
        
        if (attenuation <= 0.0) continue;

        vec3 H = normalize(V + L);

        float NoH = max(dot(N, H), 0.0);
        float NoL = max(dot(N, L), 0.0);
        float NoV = max(dot(N, V), 0.0);
        float HoV = max(dot(H, V), 0.0);

        // Cook-Torrance BRDF using engine functions
        float D = DistributionGGX(NoH, roughness);
        float G = GeometrySchlickGGX(NoL, NoV, alphaRoughness);
        vec3 F = FresnelSchlick(HoV, F0);

        vec3 numerator = D * G * F;
        float denominator = 4.0 * NoV * NoL + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        Lo += (Diffuse(kD * albedo) + specular) * lightColor * attenuation * NoL;
    }

    Lo *= pc.directLightIntensity;

    // Ambient (simple approximation)
    vec3 ambient = vec3(0.03) * albedo * ao * pc.ambientIntensity;

    vec3 color = ambient + Lo + emissive;

    // Apply exposure
    float exposure = pc.exposure > 0.0 ? pc.exposure : DEFAULT_EXPOSURE;
    color *= exposure;

    // HDR tonemapping
    if ((pc.flags & FLAG_USE_ACES) != 0u) {
        color = TonemapACES(color);
    } else {
        color = TonemapReinhard(color);
    }
    
    // Gamma correction
    color = LinearToSRGB(color);

    outColor = vec4(color, albedoSample.a);
}
