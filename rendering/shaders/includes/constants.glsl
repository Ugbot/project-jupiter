/**
 * @file constants.glsl
 * @brief Common constants for PBR shaders
 * 
 * Include this file in any shader that needs these constants.
 * Usage: #include <includes/constants.glsl>
 */

#ifndef CONSTANTS_GLSL
#define CONSTANTS_GLSL

// Mathematical constants
#ifndef PI
#define PI 3.14159265359
#endif

#ifndef TWO_PI
#define TWO_PI 6.28318530718
#endif

#ifndef HALF_PI
#define HALF_PI 1.57079632679
#endif

#ifndef INV_PI
#define INV_PI 0.31830988618
#endif

// PBR constants
const float MIN_ROUGHNESS = 0.04;       // Minimum roughness to avoid divide by zero
const float MAX_REFLECTION_LOD = 4.0;   // Maximum mip level for IBL specular sampling

// Shadow constants
const float SHADOW_AMBIENT = 0.25;      // Minimum light in shadowed areas
const int PCF_RANGE = 2;                // 5x5 kernel for soft shadows
const float PCF_SCALE = 1.0;            // Scale factor for PCF sampling

// Exposure defaults
const float DEFAULT_EXPOSURE = 1.0;

// Utility macros
#define Clamp01(x) clamp(x, 0.0, 1.0)
#define saturate(x) clamp(x, 0.0, 1.0)

#endif // CONSTANTS_GLSL

