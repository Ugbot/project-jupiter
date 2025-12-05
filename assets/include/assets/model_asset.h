#pragma once

#include "mesh_asset.h"
#include "material_asset.h"
#include <vector>
#include <string>
#include <cstdint>
#include <array>

// Forward declarations for animation types
namespace jupiter {
namespace animation {
    struct SkeletonAsset;
    struct AnimationClipAsset;
}
}

namespace jupiter {
namespace assets {

/**
 * @brief Skin data for skeletal animation (loaded from GLTF)
 *
 * Links a skeleton to mesh nodes.
 * Each skin has a skeleton root and a list of joint nodes.
 */
struct SkinAsset {
    std::string name;
    int32_t skeletonRootIndex = -1;           // Root node of the skeleton
    std::vector<int32_t> jointNodeIndices;    // Node indices for each joint
    std::vector<std::array<float, 16>> inverseBindMatrices; // Column-major 4x4 matrices
};

// ============================================================================
// Raw Animation Data (CPU-only, no GLM dependency)
// ============================================================================

/**
 * @brief Interpolation mode for animation keyframes
 */
enum class AnimationInterpolation : uint8_t {
    Step = 0,       // No interpolation, snap to nearest keyframe
    Linear = 1,     // Linear interpolation
    CubicSpline = 2 // Cubic spline interpolation
};

/**
 * @brief Transform component being animated
 */
enum class AnimationPath : uint8_t {
    Translation = 0, // vec3 position (3 floats per keyframe)
    Rotation = 1,    // quat orientation (4 floats per keyframe)
    Scale = 2        // vec3 scale (3 floats per keyframe)
};

/**
 * @brief Raw keyframe sampler data (CPU-only)
 *
 * Stores time-value pairs for animation playback.
 * Values are stored as flat float arrays:
 * - Translation: 3 floats per keyframe (xyz)
 * - Rotation: 4 floats per keyframe (xyzw quaternion)
 * - Scale: 3 floats per keyframe (xyz)
 */
struct AnimationSamplerRaw {
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;   // Keyframe timestamps in seconds
    std::vector<float> values;  // Keyframe values (flat array)

    // For cubic spline interpolation
    std::vector<float> inTangents;
    std::vector<float> outTangents;

    float getDuration() const {
        return times.empty() ? 0.0f : times.back();
    }

    uint32_t getKeyframeCount() const {
        return static_cast<uint32_t>(times.size());
    }
};

/**
 * @brief Animation channel - links sampler to target node property
 */
struct AnimationChannelRaw {
    uint32_t samplerIndex;     // Index into AnimationClipRaw::samplers
    int32_t targetNodeIndex;   // Index into ModelAsset::nodes (GLTF node index)
    AnimationPath path;        // Which transform component (T/R/S)
};

/**
 * @brief Complete raw animation clip (CPU-only, no GLM)
 *
 * Contains all keyframe data for one animation.
 * Can be converted to animation::AnimationClipAsset for playback.
 */
struct AnimationClipRaw {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationSamplerRaw> samplers;
    std::vector<AnimationChannelRaw> channels;

    void updateDuration() {
        duration = 0.0f;
        for (const auto& sampler : samplers) {
            float d = sampler.getDuration();
            if (d > duration) duration = d;
        }
    }
};

/**
 * @brief Scene graph node (no GPU resources)
 *
 * Represents a node in the model's hierarchy.
 * Contains transform and references to mesh/children.
 * No Vulkan types - pure scene graph data.
 *
 * Following CLAUDE.md:
 * - POD layout where possible
 * - Cache-friendly (transforms stored inline, not pointers)
 */
struct ModelNode {
    std::string name;  // Human-readable name

    // Transform (4x4 matrix, column-major)
    float localTransform[16];   // Local space transform
    float worldTransform[16];   // Cached world space transform (updated during load)

    // Hierarchy (indices into parent model's nodes array)
    int32_t parentIndex;               // Index of parent node (-1 = root)
    std::vector<int32_t> childIndices; // Indices of child nodes

    // Mesh reference (index into parent model's meshes array)
    int32_t meshIndex;  // -1 = no mesh (e.g., transform-only node)

    // Skin reference (index into parent model's skins array)
    int32_t skinIndex;  // -1 = no skin (static mesh)

    bool visible;  // Visibility flag

    /**
     * @brief Default constructor (identity transform)
     */
    ModelNode()
        : parentIndex(-1)
        , meshIndex(-1)
        , skinIndex(-1)
        , visible(true) {
        // Identity matrix
        for (int i = 0; i < 16; i++) {
            localTransform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            worldTransform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }
};

/**
 * @brief Complete model with scene graph (CPU-only)
 *
 * Contains all data for a 3D model:
 * - Meshes (vertex/index data)
 * - Materials (PBR properties + texture references)
 * - Scene graph (node hierarchy with transforms)
 * - Image references (UUIDs to ImageAssets in AssetDatabase)
 *
 * No Vulkan types - can be loaded without GPU context.
 *
 * Following CLAUDE.md:
 * - All data pre-allocated during load
 * - Cache-friendly layout (contiguous arrays)
 * - No runtime allocations after load
 *
 * Benefits:
 * - Server-side loading
 * - Background thread loading
 * - Asset baking pipelines
 * - Multiple renderers can share same model data
 */
struct ModelAsset {
    std::string uuid;           // Unique identifier
    std::string name;           // Human-readable name
    std::string sourceFilePath; // Original file path (for hot-reload)

    // All meshes in model (stored contiguously for cache efficiency)
    std::vector<MeshAsset> meshes;

    // All materials in model
    std::vector<MaterialAsset> materials;

    // Image asset UUIDs (actual ImageAssets stored in AssetDatabase)
    // This allows sharing images between models
    std::vector<std::string> imageUuids;

    // Scene graph (flat array for cache efficiency)
    std::vector<ModelNode> nodes;          // All nodes (roots + children)
    std::vector<int32_t> rootNodeIndices;  // Indices of root nodes

    // Skeletal animation data
    std::vector<SkinAsset> skins;          // All skins (skeleton bindings)

    // Raw animation clips (CPU-only data loaded from GLTF)
    // Can be converted to animation::AnimationClipAsset for GPU playback
    std::vector<AnimationClipRaw> animations;

    // Animation UUIDs (for reference if animations are stored separately)
    std::vector<std::string> animationUuids;

    /**
     * @brief Default constructor
     */
    ModelAsset() {}

    /**
     * @brief Get number of root nodes
     */
    size_t getRootNodeCount() const {
        return rootNodeIndices.size();
    }

    /**
     * @brief Get a root node by index
     */
    const ModelNode* getRootNode(size_t index) const {
        if (index >= rootNodeIndices.size()) return nullptr;
        int32_t nodeIdx = rootNodeIndices[index];
        if (nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(nodes.size())) return nullptr;
        return &nodes[nodeIdx];
    }

    /**
     * @brief Get a node by index
     */
    const ModelNode* getNode(size_t index) const {
        if (index >= nodes.size()) return nullptr;
        return &nodes[index];
    }

    /**
     * @brief Count total number of primitives/renderables
     */
    size_t getTotalPrimitiveCount() const {
        size_t count = 0;
        for (const auto& mesh : meshes) {
            count++;  // Each mesh is one primitive (for now)
        }
        return count;
    }

    /**
     * @brief Check if model has skeletal animation data
     */
    bool hasSkins() const {
        return !skins.empty();
    }

    /**
     * @brief Check if model has animation clips
     */
    bool hasAnimations() const {
        return !animationUuids.empty();
    }

    /**
     * @brief Get skin for a node (returns nullptr if no skin)
     */
    const SkinAsset* getSkinForNode(size_t nodeIndex) const {
        if (nodeIndex >= nodes.size()) return nullptr;
        int32_t skinIdx = nodes[nodeIndex].skinIndex;
        if (skinIdx < 0 || skinIdx >= static_cast<int32_t>(skins.size())) return nullptr;
        return &skins[skinIdx];
    }
};

} // namespace assets
} // namespace jupiter
