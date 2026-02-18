/**
 * @file simple_triangle.metal
 * @brief Simple triangle shader for Metal backend testing
 * 
 * Minimal Metal shader to test GHI Metal backend.
 * Renders a colored triangle.
 */

#include <metal_stdlib>
using namespace metal;

// Vertex input structure
struct Vertex {
    float2 position [[attribute(0)]];
    float3 color [[attribute(1)]];
};

// Vertex output / Fragment input
struct Varyings {
    float4 position [[position]];
    float3 color;
};

// Vertex shader
vertex Varyings vertexMain(Vertex in [[stage_in]]) {
    Varyings out;
    out.position = float4(in.position, 0.0, 1.0);
    out.color = in.color;
    return out;
}

// Fragment shader
fragment float4 fragmentMain(Varyings in [[stage_in]]) {
    return float4(in.color, 1.0);
}

