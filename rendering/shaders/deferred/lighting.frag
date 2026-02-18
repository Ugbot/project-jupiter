#version 460 core
#extension GL_GOOGLE_include_directive : require

/**
 * Deferred Lighting Pass Fragment Shader
 * 
 * Reconstructs material properties from G-Buffer and calculates lighting.
 */

#include "../includes/constants.glsl"
#include "../includes/pbr_functions.glsl"
#include "../includes/tonemap.glsl"
#include "../includes/shadow.glsl"

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

// Set 0: Global Resources (Camera, Lights, IBL, Shadow)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

layout(set = 0, binding = 1) uniform LightUBO {
    vec4 lightPositions[16];
    vec4 lightColors[16];
    vec4 ambientColor;
    int numLights;
} lights;

layout(set = 0, binding = 2) uniform samplerCube irradianceMap;
layout(set = 0, binding = 3) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 4) uniform sampler2D brdfLUT;

layout(set = 0, binding = 5) uniform sampler2DShadow shadowMap;
// binding 6 is SSAO in RenderGlobals, but we might pass it via G-buffer set or here?
// Usually RenderGlobals has specific slots. Let's assume we pass SSAO via G-buffer set for now.

layout(set = 0, binding = 7) uniform ShadowUBO {
    mat4 lightSpaceMatrix;
    vec4 lightPosition;
    float shadowMinBias;
    float shadowMaxBias;
    float shadowNearPlane;
    float shadowFarPlane;
} shadow;

// Set 1: G-Buffer Inputs
layout(set = 1, binding = 0) uniform sampler2D positionTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
layout(set = 1, binding = 2) uniform sampler2D albedoTex;
layout(set = 1, binding = 3) uniform sampler2D materialTex;
layout(set = 1, binding = 4) uniform sampler2D emissiveTex;
layout(set = 1, binding = 5) uniform sampler2D ssaoTex;

// Push constants
layout(push_constant) uniform PushConstants {
    vec4 viewPos;
    float directLightIntensity;
    float ambientIntensity;
    float shadowIntensity;
    float exposure;
    float maxReflectionLod;
    float lightFalloff;
    float albedoMultiplier;
    uint flags;
} pc;

// Reuse forward renderer flag bits (subset)
const uint FLAG_DISABLE_IBL = 1u << 2;

void main() {
    // Sample G-Buffer
    vec4 posSample = texture(positionTex, inUV);
    // W channel of position indicates if pixel is foreground (0.0) or background (1.0)
    // If we used a depth buffer check, we could skip skybox pixels here, but checking pos.w is easy.
    // However, in gbuffer.frag we write 0.0 for foreground.
    // Wait, let's check gbuffer.frag: "outPosition = vec4(inViewPos, 0.0);"
    // So 0.0 is foreground. But cleared to (0,0,0,1) in fillCommandBuffer?
    // In fillCommandBuffer: clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; 
    // So 1.0 is background.
    
    if (posSample.w > 0.5) {
        discard; // Background/Skybox (handled by separate pass or composition)
    }

    vec3 viewPos = posSample.xyz;
    // We need World Pos for lighting/shadows?
    // Constants.glsl / pbr_functions.glsl usually works in World Space.
    // We can reconstruct World Pos from View Pos using Inverse View Matrix.
    // But we don't have Inverse View in CameraUBO directly.
    // Actually, CameraUBO has 'view' matrix. Inverse(view) * vec4(viewPos, 1.0) = worldPos.
    // For efficiency, we can pass World Pos in G-buffer or reconstruct. 
    // G-buffer currently stores View Space Position.
    // Let's reconstruct World Position.
    
    vec4 worldPos4 = inverse(camera.view) * vec4(viewPos, 1.0);
    vec3 worldPos = worldPos4.xyz; // / worldPos4.w;

    vec3 normal = texture(normalTex, inUV).rgb; // View space normal
    // Transform normal to World Space
    // normal is direction, so use inverse transpose of view, or just inverse view rotation (if orthogonal).
    // View matrix is usually orthogonal (rotation + translation).
    // So WorldNormal = transpose(view) * ViewNormal? 
    // Or inverse(view) * ViewNormal (since it's a vector w=0).
    vec3 worldNormal = normalize(mat3(inverse(camera.view)) * normal);

    vec4 albedoSample = texture(albedoTex, inUV);
    // Albedo is stored in LINEAR space in the G-buffer
    vec3 albedo = albedoSample.rgb;
    float metallic = albedoSample.a;

    vec4 materialSample = texture(materialTex, inUV);
    float roughness = max(materialSample.r, 0.04);
    float ao = materialSample.g;

    // Emissive is stored in LINEAR space in the G-buffer
    vec3 emissive = texture(emissiveTex, inUV).rgb;
    float ssao = texture(ssaoTex, inUV).r;

    // View vector
    vec3 V = normalize(camera.cameraPosition.xyz - worldPos);
    vec3 N = normalize(worldNormal);

    // Shadow calculation
    // Using shadow.glsl functions
    // Need light space position
    vec4 lightSpacePos = shadow.lightSpaceMatrix * vec4(worldPos, 1.0);
    // We need a custom getShadowFactor function that takes params?
    // shadow.glsl usually defines functions relying on global uniforms.
    // Let's implement a simple one here reusing shadow.glsl logic if possible, 
    // or copy pbr_enhanced logic.
    
    float shadowFactor = 1.0;
    // ... calculate shadow ...
    // Assuming we have shadow map bound.
    // Simplified shadow calc:
    vec3 lightDir = normalize(shadow.lightPosition.xyz - worldPos);
    vec4 shadowCoord = lightSpacePos / lightSpacePos.w;
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    // Simple PCF would go here.
    // For now, let's just use 1.0 or copy from pbr_enhanced.
    
    // Direct Lighting
    vec3 Lo = vec3(0.0);
    
    for(int i = 0; i < lights.numLights; ++i) {
        vec3 L;
        float attenuation = 1.0;
        vec3 lightColor = lights.lightColors[i].rgb;
        float intensity = lights.lightColors[i].w;
        vec3 lightPos = lights.lightPositions[i].xyz;
        float type = lights.lightPositions[i].w; // 0=Directional, 1=Point, 2=Spot

        if(type == 0.0) { // Directional
            L = normalize(lightPos);
        } else { // Point/Spot
            vec3 dir = lightPos - worldPos;
            float dist = length(dir);
            L = normalize(dir);
            attenuation = 1.0 / (dist * dist);
            // Spot cone cutoff...
        }

        float lightShadow = 1.0;
        if (i == 0 && type == 0.0) {
             // Only first directional light casts shadows in this implementation
             // Copy shadow logic
             // lightShadow = CalculateShadow(shadowMap, shadowCoord, ...);
        }

        vec3 radiance = lightColor * intensity * attenuation * lightShadow;
        
        // Cook-Torrance BRDF (inline calculation)
        vec3 H = normalize(V + L);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float NDF = DistributionGGX(max(dot(N, H), 0.0), roughness);
        float NdotLG = max(dot(N, L), 0.0);
        float NdotVG = max(dot(N, V), 0.0);
        float alpha = AlphaDirectLighting(roughness);
        float G = GeometrySchlickGGX(NdotLG, NdotVG, alpha);
        
        vec3 num = NDF * G * F;
        float denom = 4.0 * NdotVG * NdotLG + 0.0001;
        vec3 specular = num / denom;
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;
        
        Lo += (kD * albedo / PI + specular) * radiance * NdotLG;
    }

    // Ambient: if IBL is disabled, fall back to ambientColor from LightUBO.
    vec3 ambient = vec3(0.0);
    {
        float NoV = max(dot(N, V), 0.0);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 kS = FresnelSchlickRoughness(NoV, F0, roughness);
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        float combinedAO = ao * ssao;

        if ((pc.flags & FLAG_DISABLE_IBL) != 0u) {
            // Cheap ambient: use ambientColor from UBO
            vec3 ambientLight = lights.ambientColor.rgb * lights.ambientColor.w;
            ambient = (kD * albedo) * ambientLight * combinedAO * pc.ambientIntensity;
        } else {
            vec3 irradiance = texture(irradianceMap, N).rgb;
            vec3 diffuse = irradiance * albedo;

            vec3 R = reflect(-V, N);
            float maxLod = max(pc.maxReflectionLod, 0.0);
            vec3 prefilteredColor = textureLod(prefilteredMap, R, roughness * maxLod).rgb;
            vec2 brdf = texture(brdfLUT, vec2(NoV, roughness)).rg;
            vec3 specular = prefilteredColor * (kS * brdf.x + brdf.y);

            ambient = (kD * diffuse + specular) * combinedAO * pc.ambientIntensity;
        }
    }

    vec3 color = ambient + Lo * pc.directLightIntensity + emissive;
    color *= pc.exposure;

    // Tonemapping
    color = TonemapACES(color);

    // Output is to an sRGB swapchain; convert linear -> sRGB
    color = LinearToSRGB(color);

    outColor = vec4(color, 1.0);
}
