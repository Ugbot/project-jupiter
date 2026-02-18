#version 450

// Equirectangular to cubemap conversion using MRT
// Renders all 6 cubemap faces in a single pass

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

layout(location = 0) in vec2 texCoord;

// Multiple render targets - one per cubemap face
layout(location = 0) out vec4 cubeFace0;
layout(location = 1) out vec4 cubeFace1;
layout(location = 2) out vec4 cubeFace2;
layout(location = 3) out vec4 cubeFace3;
layout(location = 4) out vec4 cubeFace4;
layout(location = 5) out vec4 cubeFace5;

// Input: Equirectangular HDR image
layout(set = 0, binding = 0) uniform sampler2D equirectMap;

// ============================================================================
// Cubemap Direction Functions
// ============================================================================

// https://en.wikipedia.org/wiki/Cube_mapping#Memory_addressing
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

// Convert Cartesian direction vector to spherical coordinates
vec2 DirToUV(vec3 dir) {
    return vec2(
        0.5 + 0.5 * atan(dir.z, dir.x) / PI,  // phi
        1.0 - acos(dir.y) / PI                 // theta
    );
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
// Main
// ============================================================================

void main() {
    vec2 texCoordTemp = texCoord * 2.0 - 1.0;
    
    for (int face = 0; face < 6; ++face) {
        vec3 position = UVToXYZ(face, texCoordTemp);
        vec3 direction = normalize(position);
        direction.y = -direction.y;  // Flip Y for correct orientation
        vec2 finalTexCoord = DirToUV(direction);
        WriteFace(face, texture(equirectMap, finalTexCoord).rgb);
    }
}
