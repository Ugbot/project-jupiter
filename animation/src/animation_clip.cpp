#include "animation/animation_clip.h"
#include <algorithm>
#include <cmath>

namespace jupiter {
namespace animation {

// ============================================================================
// Interpolation Helpers
// ============================================================================

glm::quat slerp(const glm::quat& a, const glm::quat& b, float t) {
    glm::quat result;
    float cosTheta = glm::dot(a, b);

    // If cosTheta < 0, negate one quaternion to take shorter path
    glm::quat b2 = b;
    if (cosTheta < 0.0f) {
        b2 = -b;
        cosTheta = -cosTheta;
    }

    // If quaternions are very close, use linear interpolation to avoid division by zero
    if (cosTheta > 0.9995f) {
        result = glm::normalize(glm::mix(a, b2, t));
    } else {
        float theta = std::acos(cosTheta);
        float sinTheta = std::sin(theta);
        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;
        result = wa * a + wb * b2;
    }

    return result;
}

void findKeyframes(const std::vector<float>& times, float time,
                   uint32_t& outLower, uint32_t& outUpper, float& outT) {
    const size_t count = times.size();

    if (count == 0) {
        outLower = 0;
        outUpper = 0;
        outT = 0.0f;
        return;
    }

    if (count == 1 || time <= times[0]) {
        outLower = 0;
        outUpper = 0;
        outT = 0.0f;
        return;
    }

    if (time >= times[count - 1]) {
        outLower = static_cast<uint32_t>(count - 1);
        outUpper = static_cast<uint32_t>(count - 1);
        outT = 0.0f;
        return;
    }

    // Binary search for the upper bound
    auto it = std::upper_bound(times.begin(), times.end(), time);
    outUpper = static_cast<uint32_t>(std::distance(times.begin(), it));
    outLower = outUpper - 1;

    // Calculate interpolation factor
    float timeLower = times[outLower];
    float timeUpper = times[outUpper];
    float duration = timeUpper - timeLower;

    if (duration > 0.0f) {
        outT = (time - timeLower) / duration;
    } else {
        outT = 0.0f;
    }
}

glm::vec4 sampleSampler(const AnimationSampler& sampler, float time) {
    if (sampler.times.empty() || sampler.values.empty()) {
        return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    uint32_t lower, upper;
    float t;
    findKeyframes(sampler.times, time, lower, upper, t);

    // Handle edge cases
    if (lower == upper || sampler.interpolation == Interpolation::Step) {
        return sampler.values[lower];
    }

    const glm::vec4& v0 = sampler.values[lower];
    const glm::vec4& v1 = sampler.values[upper];

    switch (sampler.interpolation) {
        case Interpolation::Step:
            return v0;

        case Interpolation::Linear:
            return glm::mix(v0, v1, t);

        case Interpolation::CubicSpline:
            // Basic linear fallback for now
            // Full cubic spline would use inTangents/outTangents
            return glm::mix(v0, v1, t);

        default:
            return v0;
    }
}

void evaluateClip(const AnimationClipAsset& clip, float time, Skeleton& skeleton) {
    const SkeletonAsset* skeletonAsset = skeleton.getAsset();
    if (!skeletonAsset) return;

    // Clamp time to clip duration
    time = std::clamp(time, 0.0f, clip.duration);

    // Temporary storage for bone transforms
    // We accumulate T/R/S separately then combine
    struct BoneTransform {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion
        glm::vec3 scale{1.0f};
        bool hasTranslation = false;
        bool hasRotation = false;
        bool hasScale = false;
    };

    const uint32_t boneCount = skeletonAsset->getBoneCount();
    std::vector<BoneTransform> transforms(boneCount);

    // Initialize with bind pose
    for (uint32_t i = 0; i < boneCount; ++i) {
        // Start with bind pose
        transforms[i].translation = glm::vec3(0.0f);
        transforms[i].rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        transforms[i].scale = glm::vec3(1.0f);
    }

    // Evaluate each channel
    for (const auto& channel : clip.channels) {
        if (channel.targetBoneIndex >= boneCount) continue;
        if (channel.samplerIndex >= clip.samplers.size()) continue;

        const AnimationSampler& sampler = clip.samplers[channel.samplerIndex];
        glm::vec4 value = sampleSampler(sampler, time);

        BoneTransform& transform = transforms[channel.targetBoneIndex];

        switch (channel.path) {
            case AnimationPath::Translation:
                transform.translation = glm::vec3(value);
                transform.hasTranslation = true;
                break;

            case AnimationPath::Rotation:
                transform.rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
                transform.hasRotation = true;
                break;

            case AnimationPath::Scale:
                transform.scale = glm::vec3(value);
                transform.hasScale = true;
                break;
        }
    }

    // Apply transforms to skeleton
    for (uint32_t i = 0; i < boneCount; ++i) {
        const BoneTransform& bt = transforms[i];

        // If no animation data for this bone, use bind pose
        if (!bt.hasTranslation && !bt.hasRotation && !bt.hasScale) {
            skeleton.setLocalTransform(i, skeletonAsset->bones[i].localBindPose);
        } else {
            // Build transform matrix: T * R * S
            glm::vec3 t = bt.hasTranslation ? bt.translation : glm::vec3(0.0f);
            glm::quat r = bt.hasRotation ? bt.rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 s = bt.hasScale ? bt.scale : glm::vec3(1.0f);

            skeleton.setLocalTRS(i, t, r, s);
        }
    }
}

} // namespace animation
} // namespace jupiter
