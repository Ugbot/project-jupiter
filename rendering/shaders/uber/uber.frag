/**
 * @file uber.frag
 * @brief Fragment shader with proper material-based lighting
 * 
 * Supports:
 * - Dielectric materials (diffuse + specular based on roughness)
 * - Metallic materials (colored specular, reduced diffuse)
 * - Variable roughness for shininess control
 * - Directional light + ambient
 * 
 * All data passed via vertex outputs to avoid UBO duplication issues with Metal.
 */
#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragColor;            // rgb = color, a = alpha
layout(location = 4) in vec4 fragMaterialProps;    // x = metallic, y = roughness
layout(location = 5) in vec4 fragSunDirIntensity;  // xyz = direction, w = intensity
layout(location = 6) in vec4 fragSunColor;         // rgb = color, a = unused
layout(location = 7) in vec4 fragAmbientColor;     // rgb = color, a = intensity
layout(location = 8) in vec3 fragCameraPos;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Optional debug mode:
    // - fragMaterialProps.z = pipelineId (0 = Simple, 1 = PBR)
    // - fragMaterialProps.w = debugEnable (set via env var on CPU)
    if (fragMaterialProps.w > 0.5) {
        float pipelineId = fragMaterialProps.z;
        vec3 debugColor = (pipelineId > 0.5) ? vec3(1.0, 0.1, 0.1) : vec3(0.1, 1.0, 0.1);
        outColor = vec4(debugColor, 1.0);
        return;
    }

    // Normalize interpolated normal
    vec3 N = normalize(fragNormal);
    
    // Material properties
    vec3 albedo = fragColor.rgb;
    float metallic = fragMaterialProps.x;
    float roughness = clamp(fragMaterialProps.y, 0.05, 1.0);  // Clamp to avoid extreme values
    
    // View direction
    vec3 V = normalize(fragCameraPos - fragWorldPos);
    
    // Light direction (from surface to light, so negate the sun direction)
    vec3 L = normalize(-fragSunDirIntensity.xyz);
    float lightIntensity = fragSunDirIntensity.w;
    vec3 lightColor = fragSunColor.rgb;
    
    // Half vector for specular
    vec3 H = normalize(V + L);
    
    // Diffuse (Lambertian)
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * NdotL * lightColor * lightIntensity;
    
    // Specular (Blinn-Phong)
    // Roughness controls shininess: low roughness = high shininess (mirror-like)
    // roughness 0.0 -> shininess 512 (very shiny)
    // roughness 0.5 -> shininess 32 (moderate)
    // roughness 1.0 -> shininess 2 (very rough, almost no specular)
    float shininess = pow(2.0, (1.0 - roughness) * 10.0);  // Exponential mapping
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, shininess);
    
    // Fresnel-like effect: more specular at glancing angles
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = 0.04 + (1.0 - 0.04) * pow(1.0 - NdotV, 5.0);
    
    // Metallic: specular is tinted by albedo, reduced diffuse
    // Dielectric: white specular (F0 ~0.04), full diffuse
    vec3 specColor = mix(vec3(fresnel), albedo, metallic);
    
    // Specular intensity based on roughness (rough surfaces = weaker specular)
    float specIntensity = (1.0 - roughness * 0.9);
    vec3 specular = specColor * spec * specIntensity * lightColor * lightIntensity;
    
    // Reduce diffuse for metals (they have no diffuse reflection in PBR)
    diffuse *= (1.0 - metallic * 0.95);
    
    // Ambient - rougher surfaces get slightly more ambient contribution
    float ambientBoost = 1.0 + roughness * 0.2;
    vec3 ambient = albedo * fragAmbientColor.rgb * fragAmbientColor.a * ambientBoost;
    
    // Combine
    vec3 color = ambient + diffuse + specular;
    
    // Simple tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, fragColor.a);
}
