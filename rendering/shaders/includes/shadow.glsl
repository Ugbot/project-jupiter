/**
 * @file shadow.glsl
 * @brief Shadow mapping utilities (from HelloVulkan)
 * 
 * Provides PCF and Poisson disk shadow sampling for soft shadows.
 * 
 * Usage: #include <includes/shadow.glsl>
 * Requires: poisson.glsl, constants.glsl
 */

#ifndef SHADOW_GLSL
#define SHADOW_GLSL

#include "constants.glsl"
#include "poisson.glsl"

/**
 * @brief Basic PCF shadow sampling
 * 
 * @param shadowMap Shadow depth map (comparison sampler)
 * @param shadowCoord Shadow map coordinates (xy = UV, z = depth)
 * @param bias Depth bias to prevent shadow acne
 * @param pcfRange Kernel half-size (1 = 3x3, 2 = 5x5)
 * @return Shadow factor (0 = full shadow, 1 = fully lit)
 */
float ShadowPCF(
    sampler2DShadow shadowMap,
    vec3 shadowCoord,
    float bias,
    int pcfRange
) {
    ivec2 texDim = textureSize(shadowMap, 0).xy;
    float dx = 1.0 / float(texDim.x);
    float dy = 1.0 / float(texDim.y);
    
    float shadow = 0.0;
    int count = 0;
    
    for (int x = -pcfRange; x <= pcfRange; x++) {
        for (int y = -pcfRange; y <= pcfRange; y++) {
            vec2 offset = vec2(dx * float(x), dy * float(y));
            float sample = texture(shadowMap, vec3(shadowCoord.xy + offset, shadowCoord.z - bias));
            shadow += mix(SHADOW_AMBIENT, 1.0, sample);
            count++;
        }
    }
    
    return shadow / float(count);
}

/**
 * @brief Poisson disk shadow sampling for softer shadows
 * 
 * @param shadowMap Shadow depth map (comparison sampler)
 * @param shadowCoord Shadow map coordinates
 * @param bias Depth bias
 * @param pcfRange Additional PCF range
 * @param poissonRadius Scale for Poisson samples (larger = tighter)
 * @return Shadow factor
 */
float ShadowPoisson(
    sampler2DShadow shadowMap,
    vec3 shadowCoord,
    float bias,
    int pcfRange,
    float poissonRadius
) {
    ivec2 texDim = textureSize(shadowMap, 0).xy;
    float dx = 1.0 / float(texDim.x);
    float dy = 1.0 / float(texDim.y);
    
    float shadow = 0.0;
    int count = 0;
    
    for (int x = -pcfRange; x <= pcfRange; x++) {
        for (int y = -pcfRange; y <= pcfRange; y++) {
            vec2 off = vec2(dx * float(x), dy * float(y));
            vec2 coord = GetPoissonDiskCoord(shadowCoord.xy + off, count, poissonRadius);
            
            float sample = texture(shadowMap, vec3(coord, shadowCoord.z - bias));
            shadow += mix(SHADOW_AMBIENT, 1.0, sample);
            count++;
        }
    }
    
    return shadow / float(count);
}

/**
 * @brief Calculate slope-based shadow bias
 * 
 * Adjusts bias based on surface angle to light to reduce
 * shadow acne while minimizing peter-panning.
 * 
 * @param N Surface normal
 * @param L Light direction
 * @param minBias Minimum bias value
 * @param maxBias Maximum bias value
 * @return Calculated bias
 */
float CalculateShadowBias(vec3 N, vec3 L, float minBias, float maxBias) {
    float NoL = max(dot(N, L), 0.0);
    return max(maxBias * (1.0 - NoL), minBias);
}

/**
 * @brief Full shadow calculation with world-to-light transform
 * 
 * @param shadowMap Shadow depth map
 * @param lightSpaceMatrix World to light-space transform
 * @param worldPos Fragment world position
 * @param N Surface normal
 * @param L Light direction
 * @param minBias Minimum shadow bias
 * @param maxBias Maximum shadow bias
 * @param shadowEnabled Whether shadows are enabled
 * @return Shadow factor
 */
float calculateShadow(
    sampler2DShadow shadowMap,
    mat4 lightSpaceMatrix,
    vec3 worldPos,
    vec3 N,
    vec3 L,
    float minBias,
    float maxBias,
    bool shadowEnabled
) {
    if (!shadowEnabled) {
        return 1.0;
    }
    
    // Transform to light space
    vec4 lightSpacePos = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    
    // Transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    // Return fully lit if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }
    
    // Calculate bias
    float bias = CalculateShadowBias(N, L, minBias, maxBias);
    
    // Sample with Poisson disk
    return ShadowPoisson(shadowMap, projCoords, bias, PCF_RANGE, 3000.0);
}

#endif // SHADOW_GLSL

