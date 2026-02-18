#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(vec3(-0.4, -0.8, -0.3));  // Sun direction
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = fragColor * NdotL * 2.5;
    vec3 ambient = fragColor * 0.4;
    
    outColor = vec4(diffuse + ambient, 1.0);
}

