#version 460 core

/**
 * Smooth terrain fragment shader
 *
 * Uses interpolated normals for smooth shading on Marching Cubes surfaces.
 * Material-based coloring with triplanar-ready UV coords.
 */

// Input from vertex shader
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inAO;
layout(location = 3) flat in uint inMaterialId;
layout(location = 4) in vec2 inTexCoord;

// Output
layout(location = 0) out vec4 outColor;

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 nearFarFov;
} camera;

// Light UBO
layout(set = 0, binding = 1) uniform LightUBO {
    vec4 sunDirection;   // xyz = direction (normalized), w = intensity
    vec4 sunColor;       // rgb = color, a = unused
    vec4 ambientColor;   // rgb = ambient, a = unused
} light;

// Material colors (expanded palette for terrain)
const vec3 MATERIAL_COLORS[16] = vec3[16](
    vec3(0.0, 0.0, 0.0),       // 0: Air (black)
    vec3(0.45, 0.45, 0.48),    // 1: Stone (cool gray)
    vec3(0.50, 0.35, 0.22),    // 2: Dirt (brown)
    vec3(0.35, 0.55, 0.20),    // 3: Grass (green)
    vec3(0.90, 0.82, 0.55),    // 4: Sand (warm tan)
    vec3(0.25, 0.45, 0.75),    // 5: Water (blue)
    vec3(0.55, 0.38, 0.22),    // 6: Wood (wood brown)
    vec3(0.25, 0.48, 0.18),    // 7: Leaves (dark green)
    vec3(0.25, 0.25, 0.28),    // 8: Bedrock (dark gray)
    vec3(0.52, 0.52, 0.55),    // 9: Cobblestone
    vec3(0.55, 0.50, 0.45),    // 10: Iron ore
    vec3(0.20, 0.20, 0.22),    // 11: Coal ore
    vec3(0.85, 0.90, 0.95),    // 12: Glass
    vec3(0.75, 0.55, 0.40),    // 13: Clay
    vec3(0.60, 0.60, 0.62),    // 14: Gravel
    vec3(0.70, 0.72, 0.75)     // 15: Snow
);

// Compute triplanar blending weights
vec3 getTriplanarWeights(vec3 normal) {
    vec3 blend = abs(normal);
    blend = blend * blend * blend;  // Sharper blending
    blend /= (blend.x + blend.y + blend.z + 0.0001);
    return blend;
}

void main() {
    // Normalize interpolated normal
    vec3 N = normalize(inNormal);
    
    // Get material color
    uint matIdx = min(inMaterialId, 15u);
    vec3 albedo = MATERIAL_COLORS[matIdx];
    
    // Add subtle variation based on world position (procedural detail)
    float noise = fract(sin(dot(floor(inWorldPos * 0.5), vec3(12.9898, 78.233, 45.543))) * 43758.5453);
    albedo *= 0.92 + noise * 0.16;  // +/- 8% variation
    
    // Directional lighting
    vec3 L = -normalize(light.sunDirection.xyz);
    float NdotL = max(dot(N, L), 0.0);
    
    // Smoother falloff for terrain
    float lightIntensity = NdotL * 0.7 + 0.3;  // Never fully dark
    
    // Simple specular for wet/rocky surfaces
    vec3 V = normalize(camera.cameraPosition.xyz - inWorldPos);
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specular = pow(NdotH, 32.0) * 0.15;  // Subtle specular
    
    // Combine lighting
    vec3 diffuse = albedo * light.sunColor.rgb * light.sunDirection.w * lightIntensity;
    vec3 ambient = albedo * light.ambientColor.rgb * 0.6;
    vec3 spec = light.sunColor.rgb * specular * NdotL;
    
    // Apply ambient occlusion
    float ao = max(inAO, 0.15);
    vec3 color = (ambient + diffuse + spec) * ao;
    
    // Height-based atmospheric fog
    float fogStart = 20.0;
    float fogEnd = 200.0;
    float dist = length(camera.cameraPosition.xyz - inWorldPos);
    float fog = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 0.6);
    vec3 fogColor = vec3(0.7, 0.75, 0.85);  // Blueish fog
    color = mix(color, fogColor, fog);
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}



