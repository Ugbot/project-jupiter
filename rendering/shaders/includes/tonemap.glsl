/**
 * @file tonemap.glsl
 * @brief Tonemapping functions (from HelloVulkan)
 * 
 * Provides various HDR to LDR tonemapping operators and gamma correction.
 * 
 * Usage: #include <includes/tonemap.glsl>
 */

#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

// =============================================================================
// Gamma Correction
// =============================================================================

/**
 * @brief Apply gamma correction (linear to sRGB)
 * 
 * @param color Linear color
 * @return Gamma corrected color
 */
vec3 GammaCorrect(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

/**
 * @brief Remove gamma correction (sRGB to linear)
 * 
 * @param color Gamma corrected color
 * @return Linear color
 */
vec3 GammaToLinear(vec3 color) {
    return pow(color, vec3(2.2));
}

/**
 * @brief sRGB EOTF (more accurate than simple gamma)
 * 
 * @param color Linear color
 * @return sRGB color
 */
vec3 LinearToSRGB(vec3 color) {
    vec3 low = color * 12.92;
    vec3 high = pow(color, vec3(1.0 / 2.4)) * 1.055 - 0.055;
    return mix(low, high, step(0.0031308, color));
}

// =============================================================================
// Tonemapping Operators
// =============================================================================

/**
 * @brief Simple Reinhard tonemapping
 * 
 * Basic operator that compresses all luminance values to [0,1].
 * Simple but can wash out colors in bright areas.
 * 
 * @param color HDR color
 * @return Tonemapped LDR color
 */
vec3 TonemapReinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

/**
 * @brief Extended Reinhard with white point
 * 
 * Allows control over the white point for better highlight handling.
 * 
 * @param color HDR color
 * @param whitePoint Luminance value mapped to white
 * @return Tonemapped LDR color
 */
vec3 TonemapReinhardExtended(vec3 color, float whitePoint) {
    float Lw = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float Ld = (Lw * (1.0 + Lw / (whitePoint * whitePoint))) / (1.0 + Lw);
    return color * (Ld / Lw);
}

/**
 * @brief Luminance-preserving Reinhard (from HelloVulkan)
 * 
 * Preserves color relationships better than per-channel Reinhard.
 * 
 * @param color HDR color
 * @return Tonemapped LDR color
 */
vec3 TonemapReinhardLuminance(vec3 color) {
    const float pureWhite = 1.0;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float mappedLuminance = (luminance * (1.0 + luminance / (pureWhite * pureWhite))) / (1.0 + luminance);
    return (mappedLuminance / max(luminance, 0.0001)) * color;
}

/**
 * @brief ACES Filmic tonemapping
 * 
 * Industry standard filmic curve used in film and games.
 * Good contrast and color preservation.
 * 
 * @param color HDR color
 * @return Tonemapped LDR color
 */
vec3 TonemapACES(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

/**
 * @brief Filmic tonemapping (Jim Hejl)
 * 
 * Optimized filmic curve with built-in gamma.
 * 
 * @param color HDR color
 * @return Tonemapped and gamma corrected LDR color
 */
vec3 TonemapFilmic(vec3 color) {
    vec3 x = max(vec3(0.0), color - 0.004);
    vec3 result = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return pow(result, vec3(2.2));  // Note: includes gamma
}

/**
 * @brief Partial Uncharted 2 helper
 */
vec3 Uncharted2Partial(vec3 x) {
    const float A = 0.15;  // Shoulder Strength
    const float B = 0.50;  // Linear Strength
    const float C = 0.10;  // Linear Angle
    const float D = 0.20;  // Toe Strength
    const float E = 0.02;  // Toe Numerator
    const float F = 0.30;  // Toe Denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

/**
 * @brief Uncharted 2 tonemapping
 * 
 * Highly configurable filmic curve from Uncharted 2.
 * 
 * @param color HDR color
 * @return Tonemapped LDR color
 */
vec3 TonemapUncharted2(vec3 color) {
    const float exposureBias = 2.0;
    vec3 curr = Uncharted2Partial(color * exposureBias);
    vec3 W = vec3(11.2);
    vec3 whiteScale = vec3(1.0) / Uncharted2Partial(W);
    return curr * whiteScale;
}

/**
 * @brief GT Tonemapping (Gran Turismo)
 * 
 * Modern tonemapper with good color preservation.
 * 
 * @param color HDR color
 * @return Tonemapped LDR color
 */
vec3 TonemapGT(vec3 color) {
    float P = 1.0;   // Max brightness
    float a = 1.0;   // Contrast
    float m = 0.22;  // Linear section start
    float l = 0.4;   // Linear section length
    float c = 1.33;  // Black tightness curve
    float b = 0.0;   // Black offset
    
    vec3 lo = color * pow(P * color / (color + vec3(1.0)), vec3(c)) + b;
    vec3 hi = (P * (color - m)) / (color + vec3(l)) + m;
    return mix(lo, hi, step(m, color));
}

// =============================================================================
// Exposure
// =============================================================================

/**
 * @brief Apply exposure adjustment
 * 
 * @param color Input color
 * @param exposure Exposure value (EV stops)
 * @return Exposed color
 */
vec3 ApplyExposure(vec3 color, float exposure) {
    return color * exposure;
}

/**
 * @brief Calculate luminance
 * 
 * @param color Input color
 * @return Luminance value
 */
float Luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

/**
 * @brief Calculate log luminance for auto-exposure
 * 
 * @param color Input color
 * @param epsilon Small value to avoid log(0)
 * @return Log luminance
 */
float LogLuminance(vec3 color, float epsilon) {
    return log(Luminance(color) + epsilon);
}

#endif // TONEMAP_GLSL









