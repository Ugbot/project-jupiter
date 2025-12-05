#pragma once

#include "animation/animation_export.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace jupiter {
namespace animation {

// LOD tier constants
constexpr uint32_t NUM_SKELETON_LODS = 4;
constexpr uint32_t LOD0_MAX_BONES = 64;  // Full skeleton: fingers, face, twist bones
constexpr uint32_t LOD1_MAX_BONES = 32;  // Reduced: no fingers, no face, no twist
constexpr uint32_t LOD2_MAX_BONES = 16;  // Minimal: spine chain + major limbs
constexpr uint32_t LOD3_MAX_BONES = 8;   // Essential: root, spine, arms, legs (single each)

// Bone flags for LOD categorization
enum class BoneCategory : uint8_t {
    Essential   = 0,  // Always included (root, spine, major limbs)
    Important   = 1,  // LOD 0-2 (shoulders, elbows, knees)
    Detail      = 2,  // LOD 0-1 (twist bones, clavicles)
    Fine        = 3   // LOD 0 only (fingers, face, toes)
};

/**
 * @brief Single bone in the skeleton hierarchy
 *
 * Bones are stored in parent-before-child order (topological sort).
 * This ensures parent transforms are always computed before children.
 */
struct Bone {
    std::string name;
    int32_t parentIndex;          // -1 for root bones
    glm::mat4 inverseBindMatrix;  // Transforms from mesh space to bone space
    glm::mat4 localBindPose;      // Default local transform (T*R*S)
    BoneCategory category;        // For automatic LOD assignment

    Bone()
        : parentIndex(-1)
        , inverseBindMatrix(1.0f)
        , localBindPose(1.0f)
        , category(BoneCategory::Essential) {}
};

/**
 * @brief LOD variant of a skeleton
 *
 * Each LOD has a reduced bone set with remapping from full skeleton indices.
 * Vertices reference the full skeleton's bone indices; the remap table
 * converts these to the LOD's reduced bone indices.
 */
struct SkeletonLOD {
    uint32_t boneCount;
    std::vector<uint32_t> essentialBones;  // Indices into full skeleton that we keep
    std::vector<int32_t> boneRemap;        // Full bone index -> LOD bone index (-1 if not in LOD)
    std::vector<int32_t> parentIndices;    // Parent indices for this LOD's reduced set
    std::vector<glm::mat4> inverseBindMatrices;  // Reordered for this LOD

    SkeletonLOD() : boneCount(0) {}

    // Check if a bone from the full skeleton is included in this LOD
    bool hasBone(uint32_t fullBoneIndex) const {
        return fullBoneIndex < boneRemap.size() && boneRemap[fullBoneIndex] >= 0;
    }

    // Get the LOD bone index for a full skeleton bone index
    int32_t getLODBoneIndex(uint32_t fullBoneIndex) const {
        if (fullBoneIndex >= boneRemap.size()) return -1;
        return boneRemap[fullBoneIndex];
    }
};

/**
 * @brief Complete skeleton asset with LOD variants
 *
 * Contains the full skeleton hierarchy plus pre-computed LOD variants
 * for efficient rendering at different distances.
 */
struct SkeletonAsset {
    std::string uuid;
    std::string name;
    std::vector<Bone> bones;                      // Full skeleton (up to 64 bones)
    std::vector<uint32_t> rootBoneIndices;        // Bones with parentIndex == -1
    std::array<SkeletonLOD, NUM_SKELETON_LODS> lods;  // Pre-computed LOD variants

    uint32_t getBoneCount() const { return static_cast<uint32_t>(bones.size()); }

    // Find bone by name (returns -1 if not found)
    int32_t findBone(const std::string& boneName) const {
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].name == boneName) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    // Get the appropriate LOD for a given distance
    static uint32_t getLODForDistance(float distance) {
        if (distance < 10.0f) return 0;
        if (distance < 30.0f) return 1;
        if (distance < 80.0f) return 2;
        return 3;
    }

    // Get max bone count for a LOD tier
    static uint32_t getMaxBonesForLOD(uint32_t lodLevel) {
        constexpr uint32_t maxBones[NUM_SKELETON_LODS] = {
            LOD0_MAX_BONES, LOD1_MAX_BONES, LOD2_MAX_BONES, LOD3_MAX_BONES
        };
        return (lodLevel < NUM_SKELETON_LODS) ? maxBones[lodLevel] : LOD3_MAX_BONES;
    }
};

/**
 * @brief Runtime skeleton instance for animation playback
 *
 * Holds the current pose of a skeleton (local and world transforms).
 * Updated each frame by the animation system.
 */
class Skeleton {
public:
    explicit Skeleton(const SkeletonAsset* asset);
    ~Skeleton() = default;

    // Non-copyable, movable
    Skeleton(const Skeleton&) = delete;
    Skeleton& operator=(const Skeleton&) = delete;
    Skeleton(Skeleton&&) = default;
    Skeleton& operator=(Skeleton&&) = default;

    // Set local transform for a bone (used by animation evaluation)
    void setLocalTransform(uint32_t boneIndex, const glm::mat4& transform);
    void setLocalTRS(uint32_t boneIndex, const glm::vec3& translation,
                     const glm::quat& rotation, const glm::vec3& scale);

    // Get transforms
    const glm::mat4& getLocalTransform(uint32_t boneIndex) const;
    const glm::mat4& getWorldTransform(uint32_t boneIndex) const;

    // Update world transforms from local transforms (call after animation evaluation)
    void updateWorldTransforms();

    // Compute final bone matrices for GPU skinning
    // Output: boneMatrix[i] = worldTransform[i] * inverseBindMatrix[i]
    void computeBoneMatrices(glm::mat4* outMatrices, uint32_t maxBones) const;

    // Compute bone matrices for a specific LOD level
    void computeBoneMatricesLOD(glm::mat4* outMatrices, uint32_t lodLevel) const;

    // Reset to bind pose
    void resetToBindPose();

    // Accessors
    const SkeletonAsset* getAsset() const { return asset_; }
    uint32_t getBoneCount() const { return static_cast<uint32_t>(localTransforms_.size()); }

private:
    const SkeletonAsset* asset_;
    std::vector<glm::mat4> localTransforms_;   // Current local transforms
    std::vector<glm::mat4> worldTransforms_;   // Current world transforms
    bool worldTransformsDirty_;
};

// ============================================================================
// Skeleton LOD Generation Utilities
// ============================================================================

/**
 * @brief Generate LOD variants for a skeleton asset
 *
 * Automatically selects essential bones based on bone categories and
 * creates remapping tables for each LOD level.
 *
 * @param asset The skeleton asset to generate LODs for (modified in place)
 */
ANIMATION_API void generateSkeletonLODs(SkeletonAsset& asset);

/**
 * @brief Auto-categorize bones based on naming conventions
 *
 * Uses common naming patterns to assign bone categories:
 * - "root", "hips", "spine", "head": Essential
 * - "shoulder", "arm", "leg", "hand", "foot": Important
 * - "twist", "roll", "clavicle": Detail
 * - "finger", "thumb", "toe", "face", "eye", "jaw": Fine
 *
 * @param asset The skeleton asset to categorize (modified in place)
 */
ANIMATION_API void autoCategorizeoBones(SkeletonAsset& asset);

} // namespace animation
} // namespace jupiter
