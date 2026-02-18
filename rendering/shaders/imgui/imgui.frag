/**
 * @file imgui.frag
 * @brief ImGui fragment shader (no blending required)
 *
 * We render UI as opaque but use alpha-cutout for font edges.
 * This avoids relying on blend state support in the current GHI pipeline creation.
 */
#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 2) uniform sampler2D fontTex;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(fontTex, fragUV);
}




