#version 450

// Diffuse irradiance convolution
// Convolves environment cubemap over hemisphere for diffuse lighting
// Renders one cubemap face at a time

layout(location = 0) in vec2 texCoord;

// Input: Environment cubemap
layout(set = 0, binding = 0) uniform samplerCube envCubemap;

// Push constants to specify which face to render
layout(push_constant) uniform PushConstants {
    int faceIndex; // 0-5 for +X, -X, +Y, -Y, +Z, -Z
} pc;

// Output: Single color attachment (one cubemap face)
layout(location = 0) out vec4 outColor;

// ============================================================================
// Common IBL Functions (inlined)
// ============================================================================

const float PI = 3.14159265359;

// Convert cubemap face and local UV to 3D direction
vec3 UVToDirection(int faceIndex, vec2 uv) {
    // Convert UV from [0,1] to [-1,1]
    vec2 texCoord = uv * 2.0 - 1.0;

    vec3 dir;
    if (faceIndex == 0) {
        // +X
        dir = vec3(1.0, -texCoord.y, -texCoord.x);
    } else if (faceIndex == 1) {
        // -X
        dir = vec3(-1.0, -texCoord.y, texCoord.x);
    } else if (faceIndex == 2) {
        // +Y
        dir = vec3(texCoord.x, 1.0, texCoord.y);
    } else if (faceIndex == 3) {
        // -Y
        dir = vec3(texCoord.x, -1.0, -texCoord.y);
    } else if (faceIndex == 4) {
        // +Z
        dir = vec3(texCoord.x, -texCoord.y, 1.0);
    } else {
        // -Z
        dir = vec3(-texCoord.x, -texCoord.y, -1.0);
    }

    return normalize(dir);
}

// ============================================================================
// Irradiance Convolution
// ============================================================================

// Convolve hemisphere for diffuse irradiance
vec3 ConvolveDiffuse(vec3 N) {
    vec3 irradiance = vec3(0.0);

    // Build tangent space basis
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float sampleDelta = 0.025;
    uint sampleCount = 0u;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // Spherical to tangent space
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));

            // Tangent space to world space
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // Sample environment and weight by cosine
            irradiance += texture(envCubemap, sampleVec).rgb * cos(theta) * sin(theta);
            sampleCount++;
        }
    }

    irradiance = PI * irradiance / float(sampleCount);
    return irradiance;
}

void main() {
    // Convert UV to 3D direction for this cubemap face
    vec3 normal = UVToDirection(pc.faceIndex, texCoord);

    // Convolve diffuse irradiance
    vec3 irradiance = ConvolveDiffuse(normal);

    outColor = vec4(irradiance, 1.0);
}
