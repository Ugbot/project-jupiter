#version 460 core

/**
 * Skybox - vertex shader
 * 
 * Generates a cube and transforms it to clip space.
 * View matrix has translation removed for infinite distance effect.
 */

layout(location = 0) out vec3 outTexCoord;

// Camera UBO (view has no translation)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

// Cube vertices (CCW winding, facing inward)
const vec3 cubeVertices[36] = vec3[](
    // Front face
    vec3(-1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0), vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0, -1.0), vec3(-1.0,  1.0, -1.0), vec3( 1.0,  1.0, -1.0),
    // Back face
    vec3(-1.0, -1.0,  1.0), vec3( 1.0, -1.0,  1.0), vec3( 1.0,  1.0,  1.0),
    vec3(-1.0, -1.0,  1.0), vec3( 1.0,  1.0,  1.0), vec3(-1.0,  1.0,  1.0),
    // Left face
    vec3(-1.0, -1.0, -1.0), vec3(-1.0, -1.0,  1.0), vec3(-1.0,  1.0,  1.0),
    vec3(-1.0, -1.0, -1.0), vec3(-1.0,  1.0,  1.0), vec3(-1.0,  1.0, -1.0),
    // Right face
    vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0,  1.0), vec3( 1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0), vec3( 1.0,  1.0, -1.0), vec3( 1.0,  1.0,  1.0),
    // Top face
    vec3(-1.0,  1.0, -1.0), vec3(-1.0,  1.0,  1.0), vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0), vec3( 1.0,  1.0,  1.0), vec3( 1.0,  1.0, -1.0),
    // Bottom face
    vec3(-1.0, -1.0, -1.0), vec3( 1.0, -1.0,  1.0), vec3(-1.0, -1.0,  1.0),
    vec3(-1.0, -1.0, -1.0), vec3( 1.0, -1.0, -1.0), vec3( 1.0, -1.0,  1.0)
);

void main() {
    vec3 position = cubeVertices[gl_VertexIndex];
    
    // Use position as texture coordinate for cubemap sampling
    outTexCoord = position;
    
    // Transform to clip space
    vec4 clipPos = camera.viewProjection * vec4(position, 1.0);
    
    // Set Z to W so that depth = 1.0 (max depth, rendered behind everything)
    gl_Position = clipPos.xyww;
}

