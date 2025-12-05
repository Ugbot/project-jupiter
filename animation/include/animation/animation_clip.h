#pragma once

#include "animation/animation_export.h"
#include "animation/skeleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace jupiter {
namespace animation {

/**
 * @brief Interpolation mode for animation keyframes
 */
enum class Interpolation : uint8_t {
    Step,           // No interpolation, snap to nearest keyframe
    Linear,         // Linear interpolation (lerp for vec3, slerp for quat)
    CubicSpline     // Cubic spline interpolation (future: requires tangent data)
};

/**
 * @brief Transform component being animated
 */
enum class AnimationPath : uint8_t {
    Translation,    // vec3 position
    Rotation,       // quat orientation
    Scale           // vec3 scale
};

/**
 * @brief Keyframe data for a single animation channel
 *
 * Stores the time-value pairs for one property of one bone.
 * Values are stored as vec4 for uniform handling:
 * - Translation: xyz = position, w = 0
 * - Rotation: xyzw = quaternion
 * - Scale: xyz = scale, w = 1
 */
struct AnimationSampler {
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;           // Keyframe timestamps in seconds
    std::vector<glm::vec4> values;      // Keyframe values (see above)

    // For cubic spline interpolation (future)
    std::vector<glm::vec4> inTangents;
    std::vector<glm::vec4> outTangents;

    // Get the duration of this sampler
    float getDuration() const {
        return times.empty() ? 0.0f : times.back();
    }

    // Get keyframe count
    uint32_t getKeyframeCount() const {
        return static_cast<uint32_t>(times.size());
    }
};

/**
 * @brief Animation channel - links a sampler to a target bone property
 */
struct AnimationChannel {
    uint32_t samplerIndex;    // Index into AnimationClipAsset::samplers
    uint32_t targetBoneIndex; // Index into SkeletonAsset::bones
    AnimationPath path;       // Which transform component (T/R/S)
};

/**
 * @brief Complete animation clip asset
 *
 * Contains all keyframe data for animating a skeleton.
 * One clip = one animation (e.g., "walk", "run", "idle").
 */
struct AnimationClipAsset {
    std::string uuid;
    std::string name;
    float duration = 0.0f;              // Total clip duration in seconds
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;

    // Get the channel count
    uint32_t getChannelCount() const {
        return static_cast<uint32_t>(channels.size());
    }

    // Calculate duration from samplers
    void updateDuration() {
        duration = 0.0f;
        for (const auto& sampler : samplers) {
            duration = std::max(duration, sampler.getDuration());
        }
    }
};

// ============================================================================
// Animation Instance (Runtime State)
// ============================================================================

/**
 * @brief Animation playback state for a single instance
 *
 * Tracks the current playback position and state for one animated character.
 * Multiple instances can share the same AnimationClipAsset.
 */
struct AnimationInstance {
    // Asset references (non-owning)
    const SkeletonAsset* skeleton = nullptr;
    const AnimationClipAsset* clip = nullptr;

    // Playback state
    float currentTime = 0.0f;       // Current playback position in seconds
    float playbackSpeed = 1.0f;     // Playback rate multiplier
    bool playing = true;            // Is animation playing?
    bool looping = true;            // Should animation loop?

    // LOD state
    uint32_t currentLOD = 0;        // Current LOD level (0-3)
    float lodBlendWeight = 0.0f;    // For smooth LOD transitions (0 = current, 1 = target)
    uint32_t targetLOD = 0;         // Target LOD for blending

    // GPU buffer references
    uint32_t boneMatrixOffset = 0;  // Offset in bone matrix buffer (GPU)
    uint32_t instanceDataIndex = 0; // Index in instance data buffer (GPU)

    // Update time with looping logic
    void advanceTime(float deltaTime) {
        if (!playing || !clip) return;

        currentTime += deltaTime * playbackSpeed;

        if (currentTime >= clip->duration) {
            if (looping) {
                // Use fmod for smooth looping
                currentTime = std::fmod(currentTime, clip->duration);
            } else {
                currentTime = clip->duration;
                playing = false;
            }
        } else if (currentTime < 0.0f) {
            if (looping) {
                currentTime = clip->duration + std::fmod(currentTime, clip->duration);
            } else {
                currentTime = 0.0f;
                playing = false;
            }
        }
    }

    // Reset to start of animation
    void reset() {
        currentTime = 0.0f;
        playing = true;
    }

    // Set new animation clip
    void setClip(const AnimationClipAsset* newClip) {
        clip = newClip;
        currentTime = 0.0f;
    }

    // Get normalized playback position (0.0 - 1.0)
    float getNormalizedTime() const {
        if (!clip || clip->duration <= 0.0f) return 0.0f;
        return currentTime / clip->duration;
    }
};

// ============================================================================
// Animation Evaluation Utilities
// ============================================================================

/**
 * @brief Sample an animation sampler at a given time
 *
 * Performs keyframe lookup and interpolation based on the sampler's
 * interpolation mode.
 *
 * @param sampler The sampler to evaluate
 * @param time The time to sample at (clamped to sampler duration)
 * @return The interpolated value at the given time
 */
ANIMATION_API glm::vec4 sampleSampler(const AnimationSampler& sampler, float time);

/**
 * @brief Evaluate an animation clip and apply to skeleton
 *
 * Samples all channels of the clip at the given time and applies
 * the results to the skeleton's local transforms.
 *
 * @param clip The animation clip to evaluate
 * @param time The time to evaluate at
 * @param skeleton The skeleton to apply transforms to
 */
ANIMATION_API void evaluateClip(const AnimationClipAsset& clip, float time, Skeleton& skeleton);

/**
 * @brief Find the keyframe indices surrounding a time value
 *
 * Uses binary search for O(log n) lookup.
 *
 * @param times The sorted array of keyframe times
 * @param time The time to search for
 * @param outLower Output: index of keyframe at or before time
 * @param outUpper Output: index of keyframe at or after time
 * @param outT Output: interpolation factor (0.0 = lower, 1.0 = upper)
 */
ANIMATION_API void findKeyframes(const std::vector<float>& times, float time,
                                  uint32_t& outLower, uint32_t& outUpper, float& outT);

/**
 * @brief Spherical linear interpolation for quaternions
 */
ANIMATION_API glm::quat slerp(const glm::quat& a, const glm::quat& b, float t);

} // namespace animation
} // namespace jupiter
