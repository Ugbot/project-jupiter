/**
 * @file light_ubo.glsl
 * @brief Light data structures for shaders
 * 
 * Defines light UBO and related structures used for PBR lighting.
 * 
 * Usage: #include <includes/light_ubo.glsl>
 */

#ifndef LIGHT_UBO_GLSL
#define LIGHT_UBO_GLSL

// Maximum number of lights supported in forward rendering
#define MAX_LIGHTS 16

/**
 * @brief Light types
 */
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

/**
 * @brief Individual light data structure
 * 
 * Used in SSBOs for clustered/bindless lighting.
 */
struct LightData {
    vec4 position;   // .xyz = position (or direction for directional), .w = type
    vec4 color;      // .xyz = color, .w = intensity
    float radius;    // Effective radius for culling
    float _pad0;
    float _pad1;
    float _pad2;
};

/**
 * @brief Light uniform buffer object
 * 
 * Traditional forward rendering light buffer with fixed array.
 */
struct LightUBO {
    vec4 lightPositions[MAX_LIGHTS];  // .xyz = position/direction, .w = type
    vec4 lightColors[MAX_LIGHTS];     // .xyz = color, .w = intensity
    vec4 ambientColor;                // .xyz = ambient color, .w = ambient intensity
    int numLights;                    // Number of active lights
};

/**
 * @brief Shadow effects uniform buffer
 * 
 * Contains shadow mapping parameters and enable flags.
 */
struct ShadowEffectsUBO {
    mat4 lightSpaceMatrix;    // World to light space transform
    vec4 shadowParams;        // x=minBias, y=maxBias, z=unused, w=unused
    int shadowEnabled;        // Shadow mapping enabled flag
    int ssaoEnabled;          // SSAO enabled flag
    float ssaoIntensity;      // SSAO intensity multiplier
    int _padding0;
};

#endif // LIGHT_UBO_GLSL

