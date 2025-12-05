#version 460 core

/**
 * Screen-Space Ambient Occlusion (SSAO) - fragment shader
 * 
 * Samples hemisphere kernel around each fragment and compares depths
 * to calculate ambient occlusion.
 */

layout(location = 0) in vec2 texCoord;
layout(location = 0) out float fragColor;

// SSAO UBO
layout(set = 0, binding = 0) uniform SSAOUBO {
    mat4 projection;
    float radius;
    float bias;
    float power;
    float screenWidth;
    float screenHeight;
    float noiseSize;
} ubo;

// SSAO hemisphere kernel
layout(set = 0, binding = 1) readonly buffer Kernels {
    vec3 kernels[];
};

// G-buffer textures
layout(set = 0, binding = 2) uniform sampler2D gPosition;  // View-space position
layout(set = 0, binding = 3) uniform sampler2D gNormal;    // View-space normal
layout(set = 0, binding = 4) uniform sampler2D texNoise;   // Random rotation vectors

void main() {
    // Scale noise texture coordinates to tile across screen
    vec2 noiseScale = vec2(ubo.screenWidth / ubo.noiseSize, ubo.screenHeight / ubo.noiseSize);

    // Sample G-buffer
    vec4 fragPos = texture(gPosition, texCoord);
    vec3 normal = normalize(texture(gNormal, texCoord).rgb);
    vec3 randomVec = normalize(vec3(texture(texNoise, texCoord * noiseScale).xy, 0.0));

    // Check if this is background (w = 1.0 in our G-buffer means background)
    float discardFactor = 1.0 - fragPos.w;
    if (discardFactor < 0.5) {
        fragColor = 1.0;  // No occlusion for background
        return;
    }

    // Create TBN matrix for orienting kernel along surface normal
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    int kernelSize = kernels.length();

    for (int i = 0; i < kernelSize; ++i) {
        // Transform kernel sample from tangent to view space
        vec3 samplePos = TBN * kernels[i];
        samplePos = fragPos.xyz + samplePos * ubo.radius;

        // Project sample position to screen space
        vec4 offset = vec4(samplePos, 1.0);
        offset = ubo.projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        // Flip Y for Vulkan
        offset.y = 1.0 - offset.y;

        // Sample depth at projected position
        float sampleDepth = texture(gPosition, offset.xy).z;

        // Range check to prevent distant geometry from affecting occlusion
        float rangeCheck = smoothstep(0.0, 1.0, ubo.radius / abs(fragPos.z - sampleDepth));
        
        // Check if sample is occluded
        occlusion += (sampleDepth >= samplePos.z + ubo.bias ? 1.0 : 0.0) * rangeCheck;
    }

    // Average and invert (1 = no occlusion, 0 = full occlusion)
    occlusion = 1.0 - (occlusion / float(kernelSize));

    // Apply power for contrast control
    fragColor = pow(occlusion, ubo.power);
}

