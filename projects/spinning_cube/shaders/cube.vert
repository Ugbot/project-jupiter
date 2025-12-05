#version 450

// Vertex attributes (2D position + color from Application framework)
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;

void main() {
    // Convert 2D position to clip space
    gl_Position = vec4(inPosition, 0.0, 1.0);

    // Pass color to fragment shader
    fragColor = inColor;
}
