#version 450

// Diffuse irradiance convolution using MRT
// Convolves environment cubemap over hemisphere for diffuse lighting
// Renders all 6 cubemap faces in a single pass

layout(location = 0) in vec2 texCoord;

// Input: Environment cubemap
layout(set = 0, binding = 0) uniform samplerCube envCubemap;

// Push constants for sample count
layout(push_constant) uniform PushConstants {
    float roughness;     // Unused for diffuse
    uint sampleCount;    // Number of samples
} pc;

// Multiple render targets - one per cubemap face
layout(location = 0) out vec4 cubeFace0;
layout(location = 1) out vec4 cubeFace1;
layout(location = 2) out vec4 cubeFace2;
layout(location = 3) out vec4 cubeFace3;
layout(location = 4) out vec4 cubeFace4;
layout(location = 5) out vec4 cubeFace5;

// ============================================================================
// Common IBL Functions
// ============================================================================

const float PI = 3.14159265359;

// Convert cubemap face and local UV to 3D direction
vec3 UVToXYZ(int face, vec2 uv) {
    if (face == 0) { 
        return vec3(1.0, uv.y, -uv.x);  // +X
    } else if (face == 1) { 
        return vec3(-1.0, uv.y, uv.x);  // -X
    } else if (face == 2) { 
        return vec3(uv.x, -1.0, uv.y);  // +Y
    } else if (face == 3) { 
        return vec3(uv.x, 1.0, -uv.y);  // -Y
    } else if (face == 4) { 
        return vec3(uv.x, uv.y, 1.0);   // +Z
    } else { 
        return vec3(-uv.x, uv.y, -1.0); // -Z
    }
}

void WriteFace(int face, vec3 colorIn) {
    vec4 color = vec4(colorIn.rgb, 1.0);
    
    if (face == 0) {
        cubeFace0 = color;
    } else if (face == 1) {
        cubeFace1 = color;
    } else if (face == 2) {
        cubeFace2 = color;
    } else if (face == 3) {
        cubeFace3 = color;
    } else if (face == 4) {
        cubeFace4 = color;
    } else {
        cubeFace5 = color;
    }
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
    vec2 texCoordTemp = texCoord * 2.0 - 1.0;
    
    for (int face = 0; face < 6; ++face) {
        vec3 position = UVToXYZ(face, texCoordTemp);
        vec3 normal = normalize(position);
        
        vec3 irradiance = ConvolveDiffuse(normal);
        WriteFace(face, irradiance);
    }
}
