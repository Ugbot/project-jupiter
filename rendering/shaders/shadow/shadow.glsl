/**
 * Shadow calculation functions
 * 
 * Include this file in your PBR shader to sample the shadow map.
 * Requires:
 * - shadowMap: sampler2DShadow bound in your descriptor set
 * - shadowUBO: ShadowUBO uniform buffer
 * - worldPos: world position of fragment
 * - normal: surface normal
 */

#ifndef SHADOW_GLSL
#define SHADOW_GLSL

// Poisson disk sampling for soft shadows
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845),
    vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507),
    vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790)
);

// Shadow ambient - minimum light when in shadow
const float SHADOW_AMBIENT = 0.15;

/**
 * Calculate shadow coordinates from world position
 */
vec4 calculateShadowCoord(mat4 lightSpaceMatrix, vec3 worldPosition) {
    vec4 shadowCoord = lightSpaceMatrix * vec4(worldPosition, 1.0);
    // Perspective divide
    shadowCoord.xyz /= shadowCoord.w;
    // Transform from [-1,1] to [0,1] for texture sampling
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
    return shadowCoord;
}

/**
 * Simple shadow lookup (hard shadows)
 */
float shadowSimple(sampler2DShadow shadowMap, vec4 shadowCoord, float bias) {
    // Clamp to valid texture coordinates
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;  // Outside shadow map = lit
    }

    // Sample with hardware comparison
    float shadow = texture(shadowMap, vec3(shadowCoord.xy, shadowCoord.z - bias));
    return mix(SHADOW_AMBIENT, 1.0, shadow);
}

/**
 * PCF (Percentage Closer Filtering) soft shadows
 */
float shadowPCF(sampler2DShadow shadowMap, vec4 shadowCoord, float bias, int kernelSize) {
    // Clamp to valid texture coordinates
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }

    // Get shadow map texel size
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    
    float shadow = 0.0;
    int halfKernel = kernelSize / 2;
    int samples = 0;

    // Sample in a grid pattern
    for (int x = -halfKernel; x <= halfKernel; x++) {
        for (int y = -halfKernel; y <= halfKernel; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            shadow += texture(shadowMap, vec3(shadowCoord.xy + offset, shadowCoord.z - bias));
            samples++;
        }
    }

    shadow /= float(samples);
    return mix(SHADOW_AMBIENT, 1.0, shadow);
}

/**
 * Poisson disk soft shadows (higher quality, fewer samples)
 */
float shadowPoisson(sampler2DShadow shadowMap, vec4 shadowCoord, float bias, float spread) {
    // Clamp to valid texture coordinates
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;

    for (int i = 0; i < 16; i++) {
        vec2 offset = poissonDisk[i] * texelSize * spread;
        shadow += texture(shadowMap, vec3(shadowCoord.xy + offset, shadowCoord.z - bias));
    }

    shadow /= 16.0;
    return mix(SHADOW_AMBIENT, 1.0, shadow);
}

/**
 * Calculate slope-scaled bias to prevent shadow acne
 */
float calculateShadowBias(vec3 normal, vec3 lightDir, float minBias, float maxBias) {
    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    return max(maxBias * (1.0 - cosTheta), minBias);
}

/**
 * Full shadow calculation with automatic bias
 */
float calculateShadow(
    sampler2DShadow shadowMap,
    mat4 lightSpaceMatrix,
    vec3 worldPos,
    vec3 normal,
    vec3 lightPos,
    float minBias,
    float maxBias,
    bool usePCF
) {
    // Calculate light direction
    vec3 lightDir = normalize(lightPos - worldPos);
    
    // Calculate shadow coordinates
    vec4 shadowCoord = calculateShadowCoord(lightSpaceMatrix, worldPos);
    
    // Calculate slope-scaled bias
    float bias = calculateShadowBias(normal, lightDir, minBias, maxBias);
    
    // Sample shadow map
    if (usePCF) {
        return shadowPCF(shadowMap, shadowCoord, bias, 3);
    } else {
        return shadowSimple(shadowMap, shadowCoord, bias);
    }
}

#endif // SHADOW_GLSL

