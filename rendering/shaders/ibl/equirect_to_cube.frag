#version 450

// Equirectangular to cubemap conversion
// Renders one cubemap face at a time

layout(location = 0) in vec2 texCoord;

// Input: Equirectangular HDR image
layout(set = 0, binding = 0) uniform sampler2D equirectMap;

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

// Convert direction vector to UV coordinates for equirectangular mapping
vec2 DirToEquirectUV(vec3 dir) {
    vec2 uv = vec2(atan(dir.z, dir.x), asin(dir.y));
    uv *= vec2(0.1591, 0.3183); // inverse atan
    uv += 0.5;
    return uv;
}

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
// Main
// ============================================================================

void main() {
    // Convert UV to 3D direction for this cubemap face
    vec3 direction = UVToDirection(pc.faceIndex, texCoord);

    // Convert direction to equirectangular UV
    vec2 equirectUV = DirToEquirectUV(direction);

    // Sample from equirectangular map
    vec3 color = texture(equirectMap, equirectUV).rgb;

    outColor = vec4(color, 1.0);
}
