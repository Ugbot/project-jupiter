#version 460 core

/**
 * Skybox - fragment shader
 * 
 * Samples environment cubemap.
 */

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform samplerCube envCubemap;

void main() {
    vec3 envColor = texture(envCubemap, inTexCoord).rgb;
    
    // Output HDR color (will be tonemapped later if HDR pipeline is active)
    fragColor = vec4(envColor, 1.0);
}

