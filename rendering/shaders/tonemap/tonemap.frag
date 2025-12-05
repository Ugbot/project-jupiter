#version 460 core

/**
 * HDR Tonemapping - fragment shader
 * 
 * Applies tonemapping operator and gamma correction to HDR input.
 * Supports both manual exposure (push constant) and auto-exposure (buffer).
 */

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D hdrImage;

// Optional: Auto-exposure buffer
// When autoExposure = 1, reads exposure from this buffer
layout(set = 0, binding = 1) readonly buffer ExposureBuffer {
    uint histogram[256];       // Not used in this shader
    float averageLuminance;    // Scene average luminance
    float currentExposure;     // Auto-calculated exposure
    float targetExposure;      // Target exposure
    float _padding;
} exposureBuffer;

layout(push_constant) uniform PushConstants {
    float exposure;       // Manual exposure value (used when autoExposure = 0)
    float gamma;          // Gamma correction value (typically 2.2)
    int operator_;        // Tonemapping operator: 0=ACES, 1=Reinhard, 2=Uncharted2, 3=None
    int autoExposure;     // 0 = use push constant exposure, 1 = use buffer exposure
} pc;

// ACES filmic tonemapping
// Based on the ACES approximation by Krzysztof Narkowicz
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Simple Reinhard tonemapping
vec3 Reinhard(vec3 x) {
    return x / (1.0 + x);
}

// Extended Reinhard with white point
vec3 ReinhardExtended(vec3 x, float whitePoint) {
    vec3 numerator = x * (1.0 + x / (whitePoint * whitePoint));
    return numerator / (1.0 + x);
}

// Uncharted 2 filmic tonemapping
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;  // Shoulder strength
    float B = 0.50;  // Linear strength
    float C = 0.10;  // Linear angle
    float D = 0.20;  // Toe strength
    float E = 0.02;  // Toe numerator
    float F = 0.30;  // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 color) {
    float exposureBias = 2.0;
    vec3 curr = Uncharted2Tonemap(exposureBias * color);
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(11.2));
    return curr * whiteScale;
}

// Simple exposure-only (no tonemapping curve)
vec3 ExposureOnly(vec3 x) {
    return clamp(x, 0.0, 1.0);
}

void main() {
    // Sample HDR color
    vec3 hdrColor = texture(hdrImage, texCoord).rgb;
    
    // Get exposure value - either from auto-exposure buffer or push constant
    float exposureValue;
    if (pc.autoExposure != 0) {
        // Use auto-calculated exposure from compute pipeline
        exposureValue = exposureBuffer.currentExposure;
    } else {
        // Use manual exposure from push constant
        exposureValue = pc.exposure;
    }
    
    // Apply exposure
    hdrColor *= exposureValue;
    
    // Apply tonemapping operator
    vec3 mapped;
    if (pc.operator_ == 0) {
        mapped = ACESFilm(hdrColor);
    } else if (pc.operator_ == 1) {
        mapped = Reinhard(hdrColor);
    } else if (pc.operator_ == 2) {
        mapped = Uncharted2(hdrColor);
    } else {
        mapped = ExposureOnly(hdrColor);
    }
    
    // Apply gamma correction
    vec3 gammaCorrected = pow(mapped, vec3(1.0 / pc.gamma));
    
    fragColor = vec4(gammaCorrected, 1.0);
}

