#version 450

// Vertex attributes (Vertex3DSkinned layout)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in uvec4 inJoints;   // Bone indices (uint8 packed)
layout(location = 5) in vec4 inWeights;   // Bone weights

// Per-instance data (instanced rendering for many characters)
layout(location = 6) in vec4 inInstanceRow0;  // Model matrix row 0 + bone texture U
layout(location = 7) in vec4 inInstanceRow1;  // Model matrix row 1 + bone texture V
layout(location = 8) in vec4 inInstanceRow2;  // Model matrix row 2 + bone texture info

// Uniforms
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
} camera;

// Bone matrix texture (RGBA32F, each bone = 3 texels for 3 rows of 4x3 matrix)
layout(set = 0, binding = 1) uniform sampler2D boneTexture;

// Output to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

// Fetch bone matrix from texture
// Each bone uses 3 horizontal texels (one per matrix row)
// boneIndex = which bone, boneTexInfo = (u_offset, v_offset, tex_width, tex_height)
mat4 fetchBoneMatrix(uint boneIndex, vec4 boneTexInfo) {
    float texWidth = boneTexInfo.z;
    float texHeight = boneTexInfo.w;

    // Each bone takes 3 texels horizontally (rows 0, 1, 2 of the 4x4 matrix)
    // Row 3 is always [0, 0, 0, 1] for affine transforms
    float u = (boneTexInfo.x + float(boneIndex) * 3.0 + 0.5) / texWidth;
    float v = (boneTexInfo.y + 0.5) / texHeight;

    vec4 row0 = texture(boneTexture, vec2(u, v));
    vec4 row1 = texture(boneTexture, vec2(u + 1.0 / texWidth, v));
    vec4 row2 = texture(boneTexture, vec2(u + 2.0 / texWidth, v));

    // Construct 4x4 matrix (column-major)
    return mat4(
        vec4(row0.x, row1.x, row2.x, 0.0),
        vec4(row0.y, row1.y, row2.y, 0.0),
        vec4(row0.z, row1.z, row2.z, 0.0),
        vec4(row0.w, row1.w, row2.w, 1.0)
    );
}

void main() {
    // Extract instance data
    mat4 modelMatrix = mat4(
        vec4(inInstanceRow0.xyz, 0.0),
        vec4(inInstanceRow1.xyz, 0.0),
        vec4(inInstanceRow2.xyz, 0.0),
        vec4(inInstanceRow0.w, inInstanceRow1.w, inInstanceRow2.w, 1.0)
    );

    // Bone texture info: (u_offset, v_offset, width, height)
    vec4 boneTexInfo = vec4(
        float(gl_InstanceIndex % 64) * 3.0,  // U offset (3 texels per bone)
        float(gl_InstanceIndex / 64),         // V offset (one row per instance batch)
        1024.0,                               // Texture width
        512.0                                 // Texture height
    );

    // Normalize weights (GLTF may have unnormalized weights)
    vec4 weights = inWeights;
    float weightSum = weights.x + weights.y + weights.z + weights.w;
    if (weightSum > 0.0001) {
        weights /= weightSum;
    } else {
        weights = vec4(1.0, 0.0, 0.0, 0.0);
    }

    // Compute skinned position and normal
    mat4 skinMatrix = mat4(0.0);

    if (weights.x > 0.0) {
        skinMatrix += weights.x * fetchBoneMatrix(inJoints.x, boneTexInfo);
    }
    if (weights.y > 0.0) {
        skinMatrix += weights.y * fetchBoneMatrix(inJoints.y, boneTexInfo);
    }
    if (weights.z > 0.0) {
        skinMatrix += weights.z * fetchBoneMatrix(inJoints.z, boneTexInfo);
    }
    if (weights.w > 0.0) {
        skinMatrix += weights.w * fetchBoneMatrix(inJoints.w, boneTexInfo);
    }

    // Apply skinning
    vec4 skinnedPos = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;
    vec3 skinnedTangent = mat3(skinMatrix) * inTangent.xyz;

    // Apply model matrix (instance transform)
    vec4 worldPos = modelMatrix * skinnedPos;
    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
    vec3 worldNormal = normalize(normalMatrix * skinnedNormal);
    vec3 worldTangent = normalize(normalMatrix * skinnedTangent);

    // Output
    fragWorldPos = worldPos.xyz;
    fragNormal = worldNormal;
    fragTexCoord = inTexCoord;
    fragTangent = worldTangent;
    fragBitangent = cross(worldNormal, worldTangent) * inTangent.w;

    gl_Position = camera.viewProj * worldPos;
}
