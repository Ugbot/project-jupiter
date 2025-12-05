#version 460 core

/**
 * Shadow map depth pass - fragment shader
 * 
 * Depth-only pass - no color output needed.
 * Depth is written automatically by the pipeline.
 */

void main() {
    // Empty - depth is written automatically
    // Could add alpha testing here if needed for transparent objects
}

