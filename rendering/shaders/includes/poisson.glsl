/**
 * @file poisson.glsl
 * @brief Poisson disk sampling for soft shadows (from HelloVulkan)
 * 
 * Provides pre-computed Poisson disk samples and utilities
 * for generating randomized shadow sampling coordinates.
 * 
 * Usage: #include <includes/poisson.glsl>
 */

#ifndef POISSON_GLSL
#define POISSON_GLSL

// Number of Poisson disk samples
const int POISSON_COUNT = 16;

// Pre-computed Poisson disk samples for 16 taps
const vec2 poissonDisk[POISSON_COUNT] = vec2[](
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

/**
 * @brief Pseudo-random number generator for Poisson disk rotation
 * 
 * @param seed 3D seed value (typically world position)
 * @param i Additional seed component
 * @return Random value in [0, 1)
 */
float Rand(vec3 seed, int i) {
    vec4 seed4 = vec4(seed, float(i));
    float dotValue = dot(seed4, vec4(12.9898, 78.233, 45.164, 94.673));
    return fract(sin(dotValue) * 43758.5453);
}

/**
 * @brief Get randomized Poisson disk coordinate
 * 
 * Rotates the Poisson disk sample based on world position
 * to reduce banding artifacts.
 * 
 * @param projCoords Shadow map projection coordinates
 * @param i Sample index
 * @param radius Scale factor (larger = tighter samples)
 * @return Offset shadow map coordinate
 */
vec2 GetPoissonDiskCoord(vec2 projCoords, int i, float radius) {
    int index = int(float(POISSON_COUNT) * Rand(projCoords.xyy, i)) % POISSON_COUNT;
    return projCoords.xy + poissonDisk[index] / radius;
}

/**
 * @brief Get Poisson disk sample with rotation
 * 
 * Applies rotation to the Poisson disk based on screen position
 * to reduce structured noise patterns.
 * 
 * @param index Sample index
 * @param rotation Rotation angle in radians
 * @return Rotated Poisson disk sample
 */
vec2 GetRotatedPoissonSample(int index, float rotation) {
    float s = sin(rotation);
    float c = cos(rotation);
    vec2 poissonSample = poissonDisk[index % POISSON_COUNT];
    return vec2(
        poissonSample.x * c - poissonSample.y * s,
        poissonSample.x * s + poissonSample.y * c
    );
}

#endif // POISSON_GLSL







