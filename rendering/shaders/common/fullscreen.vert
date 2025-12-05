#version 460 core

/**
 * Fullscreen triangle vertex shader
 * 
 * Generates a fullscreen triangle without vertex buffer.
 * Uses vertex ID to generate positions and UVs.
 */

layout(location = 0) out vec2 outTexCoord;

void main() {
    // Generate fullscreen triangle from vertex ID
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    // This covers the entire screen with one triangle
    outTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outTexCoord * 2.0 - 1.0, 0.0, 1.0);
    
    // Flip Y for Vulkan coordinate system
    outTexCoord.y = 1.0 - outTexCoord.y;
}

