#version 450

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

// Output
layout(location = 0) out vec4 outColor;

// Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
} camera;

// Simple directional light
const vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
const vec3 lightColor = vec3(1.0, 0.98, 0.95);
const vec3 ambientColor = vec3(0.15, 0.18, 0.25);

// Base color for the model (no texture for now)
const vec3 baseColor = vec3(0.8, 0.8, 0.85);

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos.xyz - fragWorldPos);
    vec3 L = lightDir;
    vec3 H = normalize(L + V);

    // Simple Blinn-Phong lighting
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    // Diffuse
    vec3 diffuse = baseColor * lightColor * NdotL;

    // Specular (simple)
    float specPower = 32.0;
    float spec = pow(NdotH, specPower);
    vec3 specular = lightColor * spec * 0.3;

    // Ambient
    vec3 ambient = ambientColor * baseColor;

    // Final color
    vec3 color = ambient + diffuse + specular;

    // Simple tone mapping
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
