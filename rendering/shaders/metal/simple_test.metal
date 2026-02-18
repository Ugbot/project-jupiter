/**
 * @file simple_test.metal
 * @brief Ultra-simple test shader - just vertex colors, no lighting
 */

#include <metal_stdlib>
using namespace metal;

// Vertex input
struct Vertex {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float2 texCoord [[attribute(2)]];
};

// Uniforms
struct CameraUniforms {
    float4x4 view;
    float4x4 projection;
};

struct ObjectUniforms {
    float4x4 model;
};

// Varyings
struct Varyings {
    float4 position [[position]];
    float3 color;
};

// Vertex shader
vertex Varyings vertexMain(
    Vertex in [[stage_in]],
    constant CameraUniforms& camera [[buffer(0)]],
    constant ObjectUniforms& object [[buffer(1)]]
) {
    Varyings out;
    
    // Transform to clip space (PROPER 3D)
    float4 worldPos = object.model * float4(in.position, 1.0);
    float4 viewPos = camera.view * worldPos;
    out.position = camera.projection * viewPos;
    
    // Color based on normal for debug
    out.color = in.normal * 0.5 + 0.5;  // Remap -1..1 to 0..1
    
    return out;
}

// Fragment shader
fragment float4 fragmentMain(Varyings in [[stage_in]]) {
    // Output bright magenta to make SURE we can see it
    return float4(1.0, 0.0, 1.0, 1.0);  // Bright magenta - impossible to miss
}

