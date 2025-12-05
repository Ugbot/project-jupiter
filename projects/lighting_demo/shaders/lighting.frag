#version 450

// Maximum number of lights (must match C++ MAX_LIGHTS constant)
#define MAX_LIGHTS 16

// Light type constants (must match C++ LightType enum)
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT 2
#define LIGHT_TYPE_AMBIENT 3

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in mat3 fragTBN;

// Output color
layout(location = 0) out vec4 outColor;

// Camera uniform
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 viewPos;
} camera;

// Light structure (matches C++ Light struct)
struct Light {
    uint type;
    vec3 pad0;

    // Directional light data
    vec3 direction;
    float pad1;
    vec3 color;
    float intensity;

    // Point/Spot light additional data
    vec3 position;
    float pad2;

    // Spot light cone data
    float innerConeAngle;
    float outerConeAngle;

    // Attenuation
    float constant;
    float linear;
    float quadratic;
    float radius;
};

// Lights uniform buffer
layout(set = 0, binding = 2) uniform LightsUBO {
    Light lights[MAX_LIGHTS];
    uint lightCount;
} lightsData;

// Material properties
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    vec2 pad;
} material;

// Textures
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;

// Constants
const float PI = 3.14159265359;
const float EPSILON = 0.0001;

// Blinn-Phong lighting calculation
vec3 calculateBlinnPhong(vec3 lightDir, vec3 viewDir, vec3 normal, vec3 lightColor, float intensity) {
    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * intensity;

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    vec3 specular = spec * lightColor * intensity * 0.5;

    return diffuse + specular;
}

// Calculate attenuation for point/spot lights
float calculateAttenuation(vec3 lightPos, vec3 fragPos, float constant, float linear, float quadratic, float radius) {
    float distance = length(lightPos - fragPos);

    // Return 0 if beyond radius
    if (distance > radius) {
        return 0.0;
    }

    // Calculate attenuation
    float attenuation = 1.0 / (constant + linear * distance + quadratic * (distance * distance));
    return attenuation;
}

// Calculate spot light cone falloff
float calculateSpotLightFalloff(vec3 lightDir, vec3 spotDir, float innerAngle, float outerAngle) {
    float theta = dot(lightDir, normalize(-spotDir));
    float epsilon = cos(innerAngle) - cos(outerAngle);
    float intensity = clamp((theta - cos(outerAngle)) / epsilon, 0.0, 1.0);
    return intensity;
}

// Process a single light
vec3 processLight(Light light, vec3 normal, vec3 viewDir, vec3 albedo) {
    vec3 result = vec3(0.0);

    if (light.type == LIGHT_TYPE_AMBIENT) {
        // Ambient light
        result = light.color * light.intensity * albedo;
    }
    else if (light.type == LIGHT_TYPE_DIRECTIONAL) {
        // Directional light
        vec3 lightDir = normalize(-light.direction);
        result = calculateBlinnPhong(lightDir, viewDir, normal, light.color, light.intensity) * albedo;
    }
    else if (light.type == LIGHT_TYPE_POINT) {
        // Point light
        vec3 lightDir = normalize(light.position - fragWorldPos);
        float attenuation = calculateAttenuation(light.position, fragWorldPos,
                                                 light.constant, light.linear,
                                                 light.quadratic, light.radius);

        if (attenuation > EPSILON) {
            vec3 lighting = calculateBlinnPhong(lightDir, viewDir, normal, light.color, light.intensity);
            result = lighting * albedo * attenuation;
        }
    }
    else if (light.type == LIGHT_TYPE_SPOT) {
        // Spot light
        vec3 lightDir = normalize(light.position - fragWorldPos);
        float attenuation = calculateAttenuation(light.position, fragWorldPos,
                                                 light.constant, light.linear,
                                                 light.quadratic, light.radius);

        if (attenuation > EPSILON) {
            float spotFalloff = calculateSpotLightFalloff(lightDir, light.direction,
                                                          light.innerConeAngle, light.outerConeAngle);

            if (spotFalloff > EPSILON) {
                vec3 lighting = calculateBlinnPhong(lightDir, viewDir, normal, light.color, light.intensity);
                result = lighting * albedo * attenuation * spotFalloff;
            }
        }
    }

    return result;
}

void main() {
    // Sample textures
    vec4 albedoSample = texture(albedoMap, fragTexCoord);
    vec3 albedo = albedoSample.rgb * material.baseColorFactor.rgb;

    // Sample and transform normal from tangent space to world space
    vec3 normalSample = texture(normalMap, fragTexCoord).rgb;
    normalSample = normalSample * 2.0 - 1.0;  // Convert from [0,1] to [-1,1]
    vec3 normal = normalize(fragTBN * normalSample);

    // Sample metallic/roughness (if needed for future PBR)
    //vec2 metallicRoughness = texture(metallicRoughnessMap, fragTexCoord).gb;
    //float metallic = metallicRoughness.r * material.metallicFactor;
    //float roughness = metallicRoughness.g * material.roughnessFactor;

    // Sample ambient occlusion
    float ao = texture(aoMap, fragTexCoord).r;

    // Calculate view direction
    vec3 viewDir = normalize(camera.viewPos - fragWorldPos);

    // Accumulate lighting from all lights
    vec3 finalColor = vec3(0.0);

    for (uint i = 0; i < MAX_LIGHTS && i < lightsData.lightCount; ++i) {
        finalColor += processLight(lightsData.lights[i], normal, viewDir, albedo);
    }

    // Apply ambient occlusion
    finalColor *= ao;

    // Simple tone mapping (Reinhard)
    finalColor = finalColor / (finalColor + vec3(1.0));

    // Gamma correction
    finalColor = pow(finalColor, vec3(1.0/2.2));

    outColor = vec4(finalColor, albedoSample.a);
}
