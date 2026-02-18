/**
 * @file imgui.vert
 * @brief ImGui vertex shader (GHI-compatible vertex layout)
 *
 * IMPORTANT: This uses the engine's fixed Vertex3D layout:
 *   location 0: vec3 inPosition  -> x,y = pixel position, z = alpha
 *   location 1: vec3 inNormal    -> rgb color
 *   location 2: vec2 inTexCoord  -> uv
 *
 * We intentionally repurpose fields to avoid adding a second vertex layout to GHI.
 */
#version 450

layout(location = 0) in vec3 inPosition; // x,y = pos, z = alpha
layout(location = 1) in vec3 inNormal;   // rgb
layout(location = 2) in vec2 inTexCoord; // uv

layout(set = 0, binding = 0) uniform ImGuiUBO {
    mat4 proj;
} ubo;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    fragUV = inTexCoord;
    fragColor = vec4(inNormal.rgb, inPosition.z);
    gl_Position = ubo.proj * vec4(inPosition.xy, 0.0, 1.0);
}




