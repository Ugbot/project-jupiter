#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

// Uniforms
layout(set = 1, binding = 0) uniform LightingUniforms {
    vec4 sunDirIntensity;  // xyz = direction, w = intensity
    vec4 sunColor;
    vec4 ambientColor;     // rgb = color, a = intensity
} lighting;

layout(set = 1, binding = 1) uniform MaterialUniforms {
    vec4 baseColor;
    float metallic;
    float roughness;
    float pad0;
    float pad1;
} material;

// Texture (set 0, binding 2 - after camera and object)
layout(set = 0, binding = 2) uniform sampler2D albedoTexture;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Sample albedo
    vec4 albedo = texture(albedoTexture, fragTexCoord) * material.baseColor;
    
    // Normalize normal
    vec3 N = normalize(fragNormal);
    
    // Light direction (pointing FROM surface TO light)
    vec3 L = normalize(-lighting.sunDirIntensity.xyz);
    
    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo.rgb * lighting.sunColor.rgb * NdotL * lighting.sunDirIntensity.w;
    
    // Ambient
    vec3 ambient = albedo.rgb * lighting.ambientColor.rgb * lighting.ambientColor.a;
    
    // Final color
    vec3 color = diffuse + ambient;
    
    outColor = vec4(color, albedo.a);
}

