#version 450

// Vertex input attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// Uniform buffers
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 viewPos;
} camera;

layout(set = 0, binding = 1) uniform ModelUBO {
    mat4 model;
    mat4 normalMatrix;  // transpose(inverse(model)) for normal transformation
} model;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out mat3 fragTBN;

void main() {
    // Transform position to world space
    vec4 worldPos = model.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform to clip space
    gl_Position = camera.projection * camera.view * worldPos;

    // Pass through texture coordinates
    fragTexCoord = inTexCoord;

    // Transform normal to world space (using normal matrix)
    fragNormal = normalize(mat3(model.normalMatrix) * inNormal);

    // Calculate TBN matrix for normal mapping
    vec3 T = normalize(mat3(model.model) * inTangent);
    vec3 N = fragNormal;
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    // Calculate bitangent
    vec3 B = cross(N, T);

    // TBN matrix transforms from tangent space to world space
    fragTBN = mat3(T, B, N);
}
