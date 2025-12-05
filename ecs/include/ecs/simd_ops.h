#pragma once

/**
 * @file simd_ops.h
 * @brief SIMD-optimized operations for ECS kernels
 * 
 * Provides vectorized implementations of common ECS operations:
 * - Position integration (pos += vel * dt)
 * - Transform computation (TRS -> mat4)
 * - Frustum culling (AABB vs planes)
 * 
 * Uses AVX2/SSE intrinsics with scalar fallback.
 */

#include "types.h"
#include "span.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>

namespace jupiter::ecs::simd {

// ============================================================================
// SIMD Detection
// ============================================================================

/**
 * @brief Check if AVX2 is available at runtime
 */
bool hasAVX2();

/**
 * @brief Check if AVX-512 is available at runtime
 */
bool hasAVX512();

/**
 * @brief Get the optimal batch size for current CPU
 */
size_t optimalBatchSize();

// ============================================================================
// Position Integration
// ============================================================================

/**
 * @brief Integrate positions: pos += vel * dt (vectorized)
 * 
 * @param positions Output positions (will be modified in-place or from velocities)
 * @param velocities Input velocities
 * @param dt Delta time
 * @param count Number of entities to process
 * 
 * SIMD: Processes 8 vec3s per iteration on AVX2
 */
void integratePositions(
    Span<glm::vec3> positions,
    Span<const glm::vec3> velocities,
    float dt,
    size_t count
);

/**
 * @brief Integrate positions with damping: pos += vel * dt, vel *= damping
 */
void integratePositionsDamped(
    Span<glm::vec3> positions,
    Span<glm::vec3> velocities,
    float dt,
    float damping,
    size_t count
);

/**
 * @brief Integrate angular velocities into rotations
 */
void integrateRotations(
    Span<glm::quat> rotations,
    Span<const glm::vec3> angularVelocities,
    float dt,
    size_t count
);

// ============================================================================
// Transform Computation
// ============================================================================

/**
 * @brief Compute model matrices from TRS components (vectorized)
 * 
 * @param transforms Output 4x4 transformation matrices
 * @param positions Input position vectors
 * @param rotations Input quaternion rotations
 * @param scales Input scale vectors
 * @param count Number of entities to process
 * 
 * Each transform = T * R * S (translation * rotation * scale)
 */
void computeTransforms(
    Span<glm::mat4> transforms,
    Span<const glm::vec3> positions,
    Span<const glm::quat> rotations,
    Span<const glm::vec3> scales,
    size_t count
);

/**
 * @brief Compute transforms with uniform scale (faster)
 */
void computeTransformsUniformScale(
    Span<glm::mat4> transforms,
    Span<const glm::vec3> positions,
    Span<const glm::quat> rotations,
    Span<const float> scales,
    size_t count
);

/**
 * @brief Update only the translation component of transforms
 */
void updateTransformPositions(
    Span<glm::mat4> transforms,
    Span<const glm::vec3> positions,
    size_t count
);

// ============================================================================
// Frustum Culling
// ============================================================================

/**
 * @brief Axis-aligned bounding box
 */
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
    
    [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec3 extents() const { return (max - min) * 0.5f; }
};

/**
 * @brief Frustum planes (6 planes: left, right, bottom, top, near, far)
 */
struct Frustum {
    glm::vec4 planes[6];  // Each plane: (nx, ny, nz, d) where n·p + d = 0
    
    /**
     * @brief Extract frustum planes from view-projection matrix
     */
    static Frustum fromViewProj(const glm::mat4& viewProj);
};

/**
 * @brief Frustum cull a batch of AABBs (vectorized)
 * 
 * @param visibility Output visibility flags (1 = visible, 0 = culled)
 * @param bounds Input AABBs to test
 * @param frustum Frustum planes
 * @param count Number of AABBs to test
 * 
 * SIMD: Tests 8 AABBs per iteration on AVX2
 */
void frustumCull(
    Span<uint32_t> visibility,
    Span<const AABB> bounds,
    const Frustum& frustum,
    size_t count
);

/**
 * @brief Frustum cull with transformed AABBs
 * 
 * @param visibility Output visibility flags
 * @param bounds Local-space AABBs
 * @param transforms World transforms for each AABB
 * @param frustum View-space frustum
 * @param count Number of entities
 */
void frustumCullTransformed(
    Span<uint32_t> visibility,
    Span<const AABB> bounds,
    Span<const glm::mat4> transforms,
    const Frustum& frustum,
    size_t count
);

/**
 * @brief Batch frustum cull returning bitmask (max 64 entities)
 * 
 * More efficient for small batches - returns a 64-bit mask where
 * bit i is set if entity i is visible.
 */
uint64_t frustumCullBatch64(
    Span<const AABB> bounds,
    const Frustum& frustum,
    size_t count  // max 64
);

// ============================================================================
// Distance/Sorting
// ============================================================================

/**
 * @brief Compute squared distances from a point (for sorting)
 */
void computeDistancesSq(
    Span<float> distances,
    Span<const glm::vec3> positions,
    const glm::vec3& origin,
    size_t count
);

/**
 * @brief Sort indices by distance (for front-to-back or back-to-front rendering)
 */
void sortByDistance(
    Span<uint32_t> indices,
    Span<const float> distances,
    size_t count,
    bool ascending = true
);

// ============================================================================
// Batch Operations
// ============================================================================

/**
 * @brief Copy vec3 array with SIMD
 */
void copyVec3(
    Span<glm::vec3> dst,
    Span<const glm::vec3> src,
    size_t count
);

/**
 * @brief Copy mat4 array with SIMD
 */
void copyMat4(
    Span<glm::mat4> dst,
    Span<const glm::mat4> src,
    size_t count
);

/**
 * @brief Zero-fill a float array
 */
void zeroFill(
    Span<float> data,
    size_t count
);

/**
 * @brief Fill vec3 array with a constant
 */
void fillVec3(
    Span<glm::vec3> data,
    const glm::vec3& value,
    size_t count
);

// ============================================================================
// Physics Helpers
// ============================================================================

/**
 * @brief Apply forces to velocities: vel += (force * invMass) * dt
 */
void applyForces(
    Span<glm::vec3> velocities,
    Span<const glm::vec3> forces,
    Span<const float> inverseMasses,
    float dt,
    size_t count
);

/**
 * @brief Apply gravity to velocities
 */
void applyGravity(
    Span<glm::vec3> velocities,
    const glm::vec3& gravity,
    float dt,
    size_t count
);

/**
 * @brief Compute AABBs from positions and half-extents
 */
void computeAABBs(
    Span<AABB> aabbs,
    Span<const glm::vec3> positions,
    Span<const glm::vec3> halfExtents,
    size_t count
);

} // namespace jupiter::ecs::simd

