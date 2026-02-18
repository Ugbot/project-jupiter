#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace jupiter {
namespace rendering {

/**
 * @brief Vertex input description for pipeline creation
 *
 * Contains binding and attribute descriptions for vertex data.
 * Following CLAUDE.md: Data-oriented, cache-friendly layout.
 */
struct VertexInputDescription {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
};

// ============================================================================
// Vertex Format Definitions
// ============================================================================

/**
 * @brief 2D vertex with color (for simple 2D rendering)
 * Layout: pos[2] + color[3]
 * Total size: 20 bytes
 */
struct Vertex2D {
    float pos[2];
    float color[3];

    static VertexInputDescription getDescription() {
        VertexInputDescription desc;

        // Binding 0: vertex data
        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(Vertex2D);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        desc.bindings.push_back(binding);

        // Attribute 0: position
        VkVertexInputAttributeDescription attr0 = {};
        attr0.binding = 0;
        attr0.location = 0;
        attr0.format = VK_FORMAT_R32G32_SFLOAT;
        attr0.offset = offsetof(Vertex2D, pos);
        desc.attributes.push_back(attr0);

        // Attribute 1: color
        VkVertexInputAttributeDescription attr1 = {};
        attr1.binding = 0;
        attr1.location = 1;
        attr1.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr1.offset = offsetof(Vertex2D, color);
        desc.attributes.push_back(attr1);

        return desc;
    }
};

/**
 * @brief 3D vertex with color (for simple 3D rendering)
 * Layout: pos[3] + color[3]
 * Total size: 24 bytes
 */
struct Vertex3D {
    float pos[3];
    float color[3];

    static VertexInputDescription getDescription() {
        VertexInputDescription desc;

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(Vertex3D);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        desc.bindings.push_back(binding);

        // Attribute 0: position
        VkVertexInputAttributeDescription attr0 = {};
        attr0.binding = 0;
        attr0.location = 0;
        attr0.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr0.offset = offsetof(Vertex3D, pos);
        desc.attributes.push_back(attr0);

        // Attribute 1: color
        VkVertexInputAttributeDescription attr1 = {};
        attr1.binding = 0;
        attr1.location = 1;
        attr1.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr1.offset = offsetof(Vertex3D, color);
        desc.attributes.push_back(attr1);

        return desc;
    }
};

/**
 * @brief 3D vertex with full lighting data
 * Layout: pos[3] + normal[3] + texCoord[2] + tangent[4]
 * Total size: 48 bytes
 *
 * Used for PBR rendering with normal mapping.
 * tangent.w contains handedness for bitangent calculation (GLTF spec).
 */
struct Vertex3DLit {
    float pos[3];
    float normal[3];
    float texCoord[2];
    float tangent[4];  // xyz = tangent vector, w = handedness (+1 or -1)

    static VertexInputDescription getDescription() {
        VertexInputDescription desc;

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(Vertex3DLit);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        desc.bindings.push_back(binding);

        // Attribute 0: position (location 0)
        VkVertexInputAttributeDescription attr0 = {};
        attr0.binding = 0;
        attr0.location = 0;
        attr0.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr0.offset = offsetof(Vertex3DLit, pos);
        desc.attributes.push_back(attr0);

        // Attribute 1: normal (location 1)
        VkVertexInputAttributeDescription attr1 = {};
        attr1.binding = 0;
        attr1.location = 1;
        attr1.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr1.offset = offsetof(Vertex3DLit, normal);
        desc.attributes.push_back(attr1);

        // Attribute 2: texCoord (location 2)
        VkVertexInputAttributeDescription attr2 = {};
        attr2.binding = 0;
        attr2.location = 2;
        attr2.format = VK_FORMAT_R32G32_SFLOAT;
        attr2.offset = offsetof(Vertex3DLit, texCoord);
        desc.attributes.push_back(attr2);

        // Attribute 3: tangent (location 3) - vec4 with handedness in w
        VkVertexInputAttributeDescription attr3 = {};
        attr3.binding = 0;
        attr3.location = 3;
        attr3.format = VK_FORMAT_R32G32B32A32_SFLOAT;  // 4-component for handedness
        attr3.offset = offsetof(Vertex3DLit, tangent);
        desc.attributes.push_back(attr3);

        return desc;
    }
};

/**
 * @brief 3D vertex with skeletal animation data
 * Layout: pos[3] + normal[3] + texCoord[2] + tangent[4] + joints[4] + weights[4]
 * Total size: 72 bytes (aligned to 8 bytes)
 *
 * Used for GPU-driven skeletal animation.
 * - joints: uint8_t[4] bone indices (supports up to 256 bones per mesh)
 * - weights: float[4] bone weights (normalized, sum to 1.0)
 *
 * Shader usage:
 *   mat4 skinMatrix = weights[0] * boneMatrices[joints[0]]
 *                   + weights[1] * boneMatrices[joints[1]]
 *                   + weights[2] * boneMatrices[joints[2]]
 *                   + weights[3] * boneMatrices[joints[3]];
 *   vec4 skinnedPos = skinMatrix * vec4(pos, 1.0);
 *
 * Following CLAUDE.md: Optimized for GPU skinning with LOD bone reduction.
 */
struct Vertex3DSkinned {
    float pos[3];
    float normal[3];
    float texCoord[2];
    float tangent[4];      // xyz = tangent vector, w = handedness
    uint8_t joints[4];     // Bone indices (max 4 influences per vertex)
    float weights[4];      // Bone weights (normalized)

    static VertexInputDescription getDescription() {
        VertexInputDescription desc;

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(Vertex3DSkinned);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        desc.bindings.push_back(binding);

        // Attribute 0: position (location 0)
        VkVertexInputAttributeDescription attr0 = {};
        attr0.binding = 0;
        attr0.location = 0;
        attr0.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr0.offset = offsetof(Vertex3DSkinned, pos);
        desc.attributes.push_back(attr0);

        // Attribute 1: normal (location 1)
        VkVertexInputAttributeDescription attr1 = {};
        attr1.binding = 0;
        attr1.location = 1;
        attr1.format = VK_FORMAT_R32G32B32_SFLOAT;
        attr1.offset = offsetof(Vertex3DSkinned, normal);
        desc.attributes.push_back(attr1);

        // Attribute 2: texCoord (location 2)
        VkVertexInputAttributeDescription attr2 = {};
        attr2.binding = 0;
        attr2.location = 2;
        attr2.format = VK_FORMAT_R32G32_SFLOAT;
        attr2.offset = offsetof(Vertex3DSkinned, texCoord);
        desc.attributes.push_back(attr2);

        // Attribute 3: tangent (location 3)
        VkVertexInputAttributeDescription attr3 = {};
        attr3.binding = 0;
        attr3.location = 3;
        attr3.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attr3.offset = offsetof(Vertex3DSkinned, tangent);
        desc.attributes.push_back(attr3);

        // Attribute 4: joint indices (location 4) - packed as R8G8B8A8_UINT
        VkVertexInputAttributeDescription attr4 = {};
        attr4.binding = 0;
        attr4.location = 4;
        attr4.format = VK_FORMAT_R8G8B8A8_UINT;
        attr4.offset = offsetof(Vertex3DSkinned, joints);
        desc.attributes.push_back(attr4);

        // Attribute 5: joint weights (location 5)
        VkVertexInputAttributeDescription attr5 = {};
        attr5.binding = 0;
        attr5.location = 5;
        attr5.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attr5.offset = offsetof(Vertex3DSkinned, weights);
        desc.attributes.push_back(attr5);

        return desc;
    }

    /**
     * @brief Normalize bone weights to sum to 1.0
     *
     * GLTF spec allows non-normalized weights, so we normalize on load.
     * Also ensures at least one weight is non-zero.
     */
    void normalizeWeights() {
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        if (sum > 0.0001f) {
            float invSum = 1.0f / sum;
            weights[0] *= invSum;
            weights[1] *= invSum;
            weights[2] *= invSum;
            weights[3] *= invSum;
        } else {
            // No weights - bind to first bone with full weight
            weights[0] = 1.0f;
            weights[1] = 0.0f;
            weights[2] = 0.0f;
            weights[3] = 0.0f;
        }
    }

    /**
     * @brief Remap bone indices for LOD
     *
     * Used when switching to a lower LOD skeleton with fewer bones.
     * @param remapTable Maps original bone index to LOD bone index (-1 = removed)
     * @param fallbackBone Bone to use if original is removed (typically root)
     */
    void remapBonesForLOD(const int32_t* remapTable, uint8_t fallbackBone) {
        for (int i = 0; i < 4; i++) {
            int32_t remapped = remapTable[joints[i]];
            if (remapped >= 0) {
                joints[i] = static_cast<uint8_t>(remapped);
            } else {
                // Bone removed in this LOD - redistribute weight
                joints[i] = fallbackBone;
            }
        }
        normalizeWeights();
    }
};

/**
 * @brief stb_voxel_render Mode 30 vertex format (8 bytes)
 *
 * Direct output from stb_voxel_render - no conversion needed.
 * This is the most efficient format for voxel rendering.
 *
 * attr_vertex (32 bits):
 *   bits 0-6:   X position (0-127)
 *   bits 7-13:  Y position (0-127)
 *   bits 14-22: Z position (0-511)
 *   bits 23-28: Ambient occlusion (0-63)
 *   bits 29-31: Texture lerp (0-7)
 *
 * attr_face (32 bits):
 *   bits 0-5:   Color/material index (0-63)
 *   bits 6-10:  Normal index (0-31, only 0-5 used for cube faces)
 *   bits 11+:   Texture coordinates, etc.
 *
 * Total size: 8 bytes (optimal for GPU cache, direct stb output)
 */
struct VoxelVertexGPU {
    uint32_t attr_vertex;  // Packed position + AO + texlerp
    uint32_t attr_face;    // Face data (normal, color, tex)

    static VertexInputDescription getDescription() {
        VertexInputDescription desc;

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(VoxelVertexGPU);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        desc.bindings.push_back(binding);

        // Attribute 0: attr_vertex (location 0) - packed position + AO
        VkVertexInputAttributeDescription attr0 = {};
        attr0.binding = 0;
        attr0.location = 0;
        attr0.format = VK_FORMAT_R32_UINT;
        attr0.offset = offsetof(VoxelVertexGPU, attr_vertex);
        desc.attributes.push_back(attr0);

        // Attribute 1: attr_face (location 1) - packed face data
        VkVertexInputAttributeDescription attr1 = {};
        attr1.binding = 0;
        attr1.location = 1;
        attr1.format = VK_FORMAT_R32_UINT;
        attr1.offset = offsetof(VoxelVertexGPU, attr_face);
        desc.attributes.push_back(attr1);

        return desc;
    }
};

static_assert(sizeof(VoxelVertexGPU) == 8, "VoxelVertexGPU must be 8 bytes");

} // namespace rendering
} // namespace jupiter
