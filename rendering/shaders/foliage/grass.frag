#version 450

// Grass fragment shader - simple Lambert lighting

// Camera (from RenderGlobals, Set 0 Binding 0)
layout(set=0, binding=0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec4 position;
    float cameraNear;
    float cameraFar;
} camera;

// Lighting (from RenderGlobals, Set 0 Binding 1)
layout(set=0, binding=1) uniform LightingUBO {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    uint _pad0;
    vec4 directionalDirs[8];     // xyz = direction, w = intensity
    vec4 directionalColors[8];   // rgb = color
    vec4 pointPositions[32];
    vec4 pointColors[32];
    vec4 spotPositions[16];
    vec4 spotDirections[16];
    vec4 spotColors[16];
    vec4 ambientColor;  // rgb = color, w = intensity
} lighting;

// Inputs from vertex shader
layout(location=0) in vec3 fragNormal;
layout(location=1) in vec3 fragColor;
layout(location=2) in float fragAO;

// Output
layout(location=0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    
    // Get sun direction from first directional light
    vec3 sunDir = vec3(0.0, -1.0, 0.0);  // Default down
    float sunIntensity = 1.0;
    if (lighting.directionalCount > 0u) {
        sunDir = lighting.directionalDirs[0].xyz;
        sunIntensity = lighting.directionalDirs[0].w;
    }
    vec3 L = normalize(-sunDir);
    
    // Lambert diffuse
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = fragColor * NdotL * sunIntensity;
    
    // Ambient (sky contribution)
    vec3 ambient = fragColor * lighting.ambientColor.w * 0.4;
    
    // Apply ambient occlusion
    vec3 finalColor = (diffuse + ambient) * fragAO;
    
    outColor = vec4(finalColor, 1.0);
}

