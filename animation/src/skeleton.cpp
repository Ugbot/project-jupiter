#include "animation/skeleton.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>

namespace jupiter {
namespace animation {

// ============================================================================
// Skeleton Implementation
// ============================================================================

Skeleton::Skeleton(const SkeletonAsset* asset)
    : asset_(asset)
    , worldTransformsDirty_(true) {
    if (asset_) {
        const size_t boneCount = asset_->bones.size();
        localTransforms_.resize(boneCount);
        worldTransforms_.resize(boneCount);
        resetToBindPose();
    }
}

void Skeleton::setLocalTransform(uint32_t boneIndex, const glm::mat4& transform) {
    if (boneIndex < localTransforms_.size()) {
        localTransforms_[boneIndex] = transform;
        worldTransformsDirty_ = true;
    }
}

void Skeleton::setLocalTRS(uint32_t boneIndex, const glm::vec3& translation,
                           const glm::quat& rotation, const glm::vec3& scale) {
    if (boneIndex < localTransforms_.size()) {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        localTransforms_[boneIndex] = T * R * S;
        worldTransformsDirty_ = true;
    }
}

const glm::mat4& Skeleton::getLocalTransform(uint32_t boneIndex) const {
    static const glm::mat4 identity(1.0f);
    if (boneIndex < localTransforms_.size()) {
        return localTransforms_[boneIndex];
    }
    return identity;
}

const glm::mat4& Skeleton::getWorldTransform(uint32_t boneIndex) const {
    static const glm::mat4 identity(1.0f);
    if (boneIndex < worldTransforms_.size()) {
        return worldTransforms_[boneIndex];
    }
    return identity;
}

void Skeleton::updateWorldTransforms() {
    if (!worldTransformsDirty_ || !asset_) return;

    const auto& bones = asset_->bones;
    const size_t boneCount = bones.size();

    // Bones are stored in parent-before-child order, so we can iterate linearly
    for (size_t i = 0; i < boneCount; ++i) {
        const int32_t parentIdx = bones[i].parentIndex;
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < boneCount) {
            worldTransforms_[i] = worldTransforms_[parentIdx] * localTransforms_[i];
        } else {
            // Root bone - local transform is world transform
            worldTransforms_[i] = localTransforms_[i];
        }
    }

    worldTransformsDirty_ = false;
}

void Skeleton::computeBoneMatrices(glm::mat4* outMatrices, uint32_t maxBones) const {
    if (!asset_ || !outMatrices) return;

    const auto& bones = asset_->bones;
    const uint32_t boneCount = std::min(maxBones, static_cast<uint32_t>(bones.size()));

    for (uint32_t i = 0; i < boneCount; ++i) {
        // Skinning matrix = world transform * inverse bind matrix
        outMatrices[i] = worldTransforms_[i] * bones[i].inverseBindMatrix;
    }
}

void Skeleton::computeBoneMatricesLOD(glm::mat4* outMatrices, uint32_t lodLevel) const {
    if (!asset_ || !outMatrices || lodLevel >= NUM_SKELETON_LODS) return;

    const auto& lod = asset_->lods[lodLevel];
    const auto& bones = asset_->bones;

    for (uint32_t lodBoneIdx = 0; lodBoneIdx < lod.boneCount; ++lodBoneIdx) {
        const uint32_t fullBoneIdx = lod.essentialBones[lodBoneIdx];
        // Skinning matrix = world transform * inverse bind matrix
        outMatrices[lodBoneIdx] = worldTransforms_[fullBoneIdx] * bones[fullBoneIdx].inverseBindMatrix;
    }
}

void Skeleton::resetToBindPose() {
    if (!asset_) return;

    const auto& bones = asset_->bones;
    for (size_t i = 0; i < bones.size(); ++i) {
        localTransforms_[i] = bones[i].localBindPose;
    }
    worldTransformsDirty_ = true;
    updateWorldTransforms();
}

// ============================================================================
// LOD Generation Utilities
// ============================================================================

// Helper: convert string to lowercase for comparison
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Helper: check if string contains substring (case-insensitive)
static bool containsIgnoreCase(const std::string& str, const std::string& substr) {
    return toLower(str).find(toLower(substr)) != std::string::npos;
}

void autoCategorizeoBones(SkeletonAsset& asset) {
    for (auto& bone : asset.bones) {
        const std::string& name = bone.name;

        // Essential bones (LOD 0-3): main structure
        if (containsIgnoreCase(name, "root") ||
            containsIgnoreCase(name, "hips") ||
            containsIgnoreCase(name, "pelvis") ||
            containsIgnoreCase(name, "spine") ||
            containsIgnoreCase(name, "chest") ||
            containsIgnoreCase(name, "neck") ||
            containsIgnoreCase(name, "head")) {
            bone.category = BoneCategory::Essential;
        }
        // Important bones (LOD 0-2): limbs
        else if (containsIgnoreCase(name, "shoulder") ||
                 containsIgnoreCase(name, "arm") ||
                 containsIgnoreCase(name, "elbow") ||
                 containsIgnoreCase(name, "wrist") ||
                 containsIgnoreCase(name, "hand") ||
                 containsIgnoreCase(name, "leg") ||
                 containsIgnoreCase(name, "thigh") ||
                 containsIgnoreCase(name, "knee") ||
                 containsIgnoreCase(name, "ankle") ||
                 containsIgnoreCase(name, "foot")) {
            bone.category = BoneCategory::Important;
        }
        // Detail bones (LOD 0-1): twist and auxiliary
        else if (containsIgnoreCase(name, "twist") ||
                 containsIgnoreCase(name, "roll") ||
                 containsIgnoreCase(name, "clavicle") ||
                 containsIgnoreCase(name, "breast") ||
                 containsIgnoreCase(name, "belly")) {
            bone.category = BoneCategory::Detail;
        }
        // Fine bones (LOD 0 only): fingers, face, etc.
        else if (containsIgnoreCase(name, "finger") ||
                 containsIgnoreCase(name, "thumb") ||
                 containsIgnoreCase(name, "index") ||
                 containsIgnoreCase(name, "middle") ||
                 containsIgnoreCase(name, "ring") ||
                 containsIgnoreCase(name, "pinky") ||
                 containsIgnoreCase(name, "toe") ||
                 containsIgnoreCase(name, "face") ||
                 containsIgnoreCase(name, "eye") ||
                 containsIgnoreCase(name, "jaw") ||
                 containsIgnoreCase(name, "tongue") ||
                 containsIgnoreCase(name, "lip") ||
                 containsIgnoreCase(name, "brow") ||
                 containsIgnoreCase(name, "cheek") ||
                 containsIgnoreCase(name, "nose") ||
                 containsIgnoreCase(name, "ear")) {
            bone.category = BoneCategory::Fine;
        }
        // Default to Important for unknown bones
        else {
            bone.category = BoneCategory::Important;
        }
    }
}

void generateSkeletonLODs(SkeletonAsset& asset) {
    const uint32_t fullBoneCount = static_cast<uint32_t>(asset.bones.size());

    // First, auto-categorize bones if not already done
    autoCategorizeoBones(asset);

    // Define which categories are included at each LOD
    // LOD 0: All bones
    // LOD 1: Essential + Important + Detail
    // LOD 2: Essential + Important
    // LOD 3: Essential only
    auto shouldIncludeBone = [](BoneCategory cat, uint32_t lodLevel) -> bool {
        switch (lodLevel) {
            case 0: return true;  // All bones
            case 1: return cat <= BoneCategory::Detail;  // No Fine
            case 2: return cat <= BoneCategory::Important;  // No Detail or Fine
            case 3: return cat == BoneCategory::Essential;  // Essential only
            default: return false;
        }
    };

    // Generate each LOD variant
    for (uint32_t lodLevel = 0; lodLevel < NUM_SKELETON_LODS; ++lodLevel) {
        SkeletonLOD& lod = asset.lods[lodLevel];
        const uint32_t maxBones = SkeletonAsset::getMaxBonesForLOD(lodLevel);

        // Reset LOD data
        lod.essentialBones.clear();
        lod.boneRemap.resize(fullBoneCount, -1);
        lod.parentIndices.clear();
        lod.inverseBindMatrices.clear();

        // First pass: collect bones that should be included
        std::vector<uint32_t> candidateBones;
        for (uint32_t i = 0; i < fullBoneCount; ++i) {
            if (shouldIncludeBone(asset.bones[i].category, lodLevel)) {
                candidateBones.push_back(i);
            }
        }

        // If we have too many bones, prioritize by category
        // (Essential first, then Important, etc.)
        if (candidateBones.size() > maxBones) {
            std::sort(candidateBones.begin(), candidateBones.end(),
                [&asset](uint32_t a, uint32_t b) {
                    return static_cast<uint8_t>(asset.bones[a].category) <
                           static_cast<uint8_t>(asset.bones[b].category);
                });
            candidateBones.resize(maxBones);
        }

        // Sort back to original order (parent-before-child)
        std::sort(candidateBones.begin(), candidateBones.end());

        // Build the LOD data
        lod.essentialBones = candidateBones;
        lod.boneCount = static_cast<uint32_t>(candidateBones.size());

        // Build remap table
        for (uint32_t lodIdx = 0; lodIdx < lod.boneCount; ++lodIdx) {
            uint32_t fullIdx = candidateBones[lodIdx];
            lod.boneRemap[fullIdx] = static_cast<int32_t>(lodIdx);
        }

        // Build parent indices and inverse bind matrices for LOD
        lod.parentIndices.resize(lod.boneCount);
        lod.inverseBindMatrices.resize(lod.boneCount);

        for (uint32_t lodIdx = 0; lodIdx < lod.boneCount; ++lodIdx) {
            uint32_t fullIdx = candidateBones[lodIdx];
            const Bone& bone = asset.bones[fullIdx];

            // Copy inverse bind matrix
            lod.inverseBindMatrices[lodIdx] = bone.inverseBindMatrix;

            // Find parent in LOD (may need to skip removed ancestors)
            int32_t fullParent = bone.parentIndex;
            int32_t lodParent = -1;

            while (fullParent >= 0) {
                lodParent = lod.boneRemap[fullParent];
                if (lodParent >= 0) break;  // Found a parent that exists in LOD
                fullParent = asset.bones[fullParent].parentIndex;  // Skip to grandparent
            }

            lod.parentIndices[lodIdx] = lodParent;
        }
    }
}

} // namespace animation
} // namespace jupiter
