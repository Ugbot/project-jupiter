/**
 * @file pbr.frag
 * @brief PBR Fragment Shader
 * 
 * Cook-Torrance BRDF implementation for physically-based rendering.
 * Supports multiple lights (directional, point, spot) and basic PBR materials.
 */
#version 450

// Constants
const float PI = 3.14159265359;
const int MAX_LIGHTS = 16;

// Light types
const int LIGHT_DISABLED = 0;
const int LIGHT_DIRECTIONAL = 1;
const int LIGHT_POINT = 2;
const int LIGHT_SPOT = 3;

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragViewDir;

// Output
layout(location = 0) out vec4 outColor;

// Light structure (64 bytes per light)
struct Light {
    vec4 positionType;       // xyz = position, w = type
    vec4 directionIntensity; // xyz = direction, w = intensity
    vec4 colorRadius;        // rgb = color, a = radius
    vec4 coneAngles;         // x = innerCone, y = outerCone, zw = reserved
};

// Lighting uniforms (set 1, binding 0)
layout(set = 1, binding = 0) uniform LightingUniforms {
    Light lights[MAX_LIGHTS];
    vec4 ambientColor;     // rgb = color, a = intensity
    vec4 params;           // x = numLights, y = iblEnabled, z = exposure, w = gamma
} lighting;

// Material uniforms (set 1, binding 1)
layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec4 albedo;            // rgb = color, a = alpha
    vec4 metallicRoughness; // x = metallic, y = roughness, zw = reserved
    vec4 emissive;          // rgb = emissive color, a = occlusion strength
    vec4 flags;             // x = hasAlbedoTex, y = hasNormalTex, z = hasMetRoughTex, w = hasEmissiveTex
} material;

// ============================================================================
// PBR Functions - Cook-Torrance BRDF
// ============================================================================

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, 0.0001);
}

// Geometry function (Schlick-GGX)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's method for geometry
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = geometrySchlickGGX(NdotV, roughness);
    float ggx2 = geometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// Light Calculations
// ============================================================================

vec3 calculateLight(Light light, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    int lightType = int(light.positionType.w);
    
    if (lightType == LIGHT_DISABLED) {
        return vec3(0.0);
    }
    
    vec3 L;
    float attenuation = 1.0;
    
    if (lightType == LIGHT_DIRECTIONAL) {
        // Directional light
        L = normalize(-light.directionIntensity.xyz);
    } else {
        // Point or spot light
        vec3 lightPos = light.positionType.xyz;
        vec3 toLight = lightPos - fragWorldPos;
        float distance = length(toLight);
        L = normalize(toLight);
        
        // Attenuation
        float radius = light.colorRadius.a;
        attenuation = 1.0 / (distance * distance + 1.0);
        attenuation *= clamp(1.0 - (distance / radius), 0.0, 1.0);
        
        // Spot light cone
        if (lightType == LIGHT_SPOT) {
            vec3 spotDir = normalize(light.directionIntensity.xyz);
            float theta = dot(L, -spotDir);
            float innerCone = light.coneAngles.x;
            float outerCone = light.coneAngles.y;
            float epsilon = innerCone - outerCone;
            float spotEffect = clamp((theta - outerCone) / epsilon, 0.0, 1.0);
            attenuation *= spotEffect;
        }
    }
    
    vec3 H = normalize(V + L);
    vec3 lightColor = light.colorRadius.rgb;
    float intensity = light.directionIntensity.w;
    
    // Calculate F0 (reflectance at normal incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    // Specular
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    // kS is equal to Fresnel
    vec3 kS = F;
    // Energy conservation: diffuse + specular <= 1
    vec3 kD = vec3(1.0) - kS;
    // Metallic surfaces have no diffuse
    kD *= 1.0 - metallic;
    
    // Final radiance
    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = lightColor * intensity * attenuation;
    
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // Optional debug discriminator (CPU sets material.flags.w via env var)
    if (material.flags.w > 0.5) {
        outColor = vec4(1.0, 0.1, 0.1, 1.0);
        return;
    }

    // Material properties
    vec3 albedo = material.albedo.rgb;
    float metallic = material.metallicRoughness.x;
    float roughness = max(material.metallicRoughness.y, 0.05);  // Clamp to avoid divide by zero
    vec3 emissive = material.emissive.rgb;
    
    // Normal
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(fragViewDir);
    
    // Accumulate lighting
    vec3 Lo = vec3(0.0);
    
    int numLights = int(lighting.params.x);
    for (int i = 0; i < MAX_LIGHTS && i < numLights; i++) {
        Lo += calculateLight(lighting.lights[i], N, V, albedo, metallic, roughness);
    }
    
    // Ambient lighting (simple approximation when no IBL)
    vec3 ambient = lighting.ambientColor.rgb * lighting.ambientColor.a * albedo;
    
    // Combine
    vec3 color = ambient + Lo + emissive;
    
    // HDR tonemapping (Reinhard)
    float exposure = lighting.params.z;
    color = vec3(1.0) - exp(-color * exposure);
    
    // Gamma correction
    float gamma = lighting.params.w;
    color = pow(color, vec3(1.0 / gamma));
    
    outColor = vec4(color, material.albedo.a);
}

