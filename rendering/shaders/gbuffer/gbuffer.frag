#version 460 core

/**
 * G-buffer geometry pass - fragment shader
 * 
 * Outputs view-space position and normal for SSAO calculation.
 */

// Inputs from vertex shader
layout(location = 0) in vec3 inViewPos;
layout(location = 1) in vec3 inViewNormal;
layout(location = 2) in vec2 inTexCoord;

// MRT outputs
layout(location = 0) out vec4 outPosition;  // XYZ = view-space position, W = foreground flag
layout(location = 1) out vec4 outNormal;    // RGB = view-space normal

// Camera UBO for linearizing depth
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

void main() {
    // Output view-space position
    // W channel: 0.0 = foreground (geometry), 1.0 = background (sky)
    outPosition = vec4(inViewPos, 0.0);
    
    // Output normalized view-space normal
    outNormal = vec4(normalize(inViewNormal), 1.0);
}

