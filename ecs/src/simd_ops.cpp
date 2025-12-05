/**
 * @file simd_ops.cpp
 * @brief SIMD-optimized operations implementation
 * 
 * Uses AVX2 intrinsics when available, with scalar fallback.
 * All operations are designed for SOA (Structure of Arrays) data layout.
 */

#define GLM_ENABLE_EXPERIMENTAL
#include "ecs/simd_ops.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <immintrin.h>
    #define JUPITER_HAS_SSE 1
    #if defined(__AVX2__)
        #define JUPITER_HAS_AVX2 1
    #endif
    #if defined(__AVX512F__)
        #define JUPITER_HAS_AVX512 1
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
    #define JUPITER_HAS_NEON 1
#endif

namespace jupiter::ecs::simd {

// ============================================================================
// SIMD Detection
// ============================================================================

bool hasAVX2() {
#ifdef JUPITER_HAS_AVX2
    return true;
#else
    return false;
#endif
}

bool hasAVX512() {
#ifdef JUPITER_HAS_AVX512
    return true;
#else
    return false;
#endif
}

size_t optimalBatchSize() {
    if (hasAVX512()) return 512;
    if (hasAVX2()) return 256;
    return 128;
}

// ============================================================================
// Position Integration
// ============================================================================

void integratePositions(
    Span<glm::vec3> positions,
    Span<const glm::vec3> velocities,
    float dt,
    size_t count
) {
    if (count == 0) return;
    
    glm::vec3* pos = positions.ptr();
    const glm::vec3* vel = velocities.ptr();

#ifdef JUPITER_HAS_AVX2
    // Process 8 floats (2.67 vec3s) at a time
    // For vec3, we process x, y, z components separately in batches
    const __m256 dt_v = _mm256_set1_ps(dt);
    
    // Process in groups of 8 vec3s (24 floats)
    size_t i = 0;
    const size_t simdCount = (count / 8) * 8;
    
    for (; i < simdCount; i += 8) {
        // Load 8 x components
        __m256 px = _mm256_set_ps(
            pos[i+7].x, pos[i+6].x, pos[i+5].x, pos[i+4].x,
            pos[i+3].x, pos[i+2].x, pos[i+1].x, pos[i+0].x
        );
        __m256 vx = _mm256_set_ps(
            vel[i+7].x, vel[i+6].x, vel[i+5].x, vel[i+4].x,
            vel[i+3].x, vel[i+2].x, vel[i+1].x, vel[i+0].x
        );
        px = _mm256_fmadd_ps(vx, dt_v, px);
        
        // Load 8 y components
        __m256 py = _mm256_set_ps(
            pos[i+7].y, pos[i+6].y, pos[i+5].y, pos[i+4].y,
            pos[i+3].y, pos[i+2].y, pos[i+1].y, pos[i+0].y
        );
        __m256 vy = _mm256_set_ps(
            vel[i+7].y, vel[i+6].y, vel[i+5].y, vel[i+4].y,
            vel[i+3].y, vel[i+2].y, vel[i+1].y, vel[i+0].y
        );
        py = _mm256_fmadd_ps(vy, dt_v, py);
        
        // Load 8 z components
        __m256 pz = _mm256_set_ps(
            pos[i+7].z, pos[i+6].z, pos[i+5].z, pos[i+4].z,
            pos[i+3].z, pos[i+2].z, pos[i+1].z, pos[i+0].z
        );
        __m256 vz = _mm256_set_ps(
            vel[i+7].z, vel[i+6].z, vel[i+5].z, vel[i+4].z,
            vel[i+3].z, vel[i+2].z, vel[i+1].z, vel[i+0].z
        );
        pz = _mm256_fmadd_ps(vz, dt_v, pz);
        
        // Store results back
        alignas(32) float rx[8], ry[8], rz[8];
        _mm256_store_ps(rx, px);
        _mm256_store_ps(ry, py);
        _mm256_store_ps(rz, pz);
        
        for (int j = 0; j < 8; ++j) {
            pos[i + j].x = rx[j];
            pos[i + j].y = ry[j];
            pos[i + j].z = rz[j];
        }
    }
    
    // Scalar remainder
    for (; i < count; ++i) {
        pos[i] += vel[i] * dt;
    }
#else
    // Scalar fallback
    for (size_t i = 0; i < count; ++i) {
        pos[i] += vel[i] * dt;
    }
#endif
}

void integratePositionsDamped(
    Span<glm::vec3> positions,
    Span<glm::vec3> velocities,
    float dt,
    float damping,
    size_t count
) {
    glm::vec3* pos = positions.ptr();
    glm::vec3* vel = velocities.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        pos[i] += vel[i] * dt;
        vel[i] *= damping;
    }
}

void integrateRotations(
    Span<glm::quat> rotations,
    Span<const glm::vec3> angularVelocities,
    float dt,
    size_t count
) {
    glm::quat* rot = rotations.ptr();
    const glm::vec3* angVel = angularVelocities.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        // Convert angular velocity to quaternion delta
        glm::vec3 theta = angVel[i] * dt * 0.5f;
        float thetaMagSq = glm::dot(theta, theta);
        
        glm::quat deltaQ;
        if (thetaMagSq < 1e-6f) {
            // Small angle approximation
            deltaQ = glm::quat(1.0f, theta.x, theta.y, theta.z);
        } else {
            float thetaMag = std::sqrt(thetaMagSq);
            float s = std::sin(thetaMag) / thetaMag;
            deltaQ = glm::quat(std::cos(thetaMag), theta.x * s, theta.y * s, theta.z * s);
        }
        
        rot[i] = glm::normalize(deltaQ * rot[i]);
    }
}

// ============================================================================
// Transform Computation
// ============================================================================

void computeTransforms(
    Span<glm::mat4> transforms,
    Span<const glm::vec3> positions,
    Span<const glm::quat> rotations,
    Span<const glm::vec3> scales,
    size_t count
) {
    glm::mat4* xform = transforms.ptr();
    const glm::vec3* pos = positions.ptr();
    const glm::quat* rot = rotations.ptr();
    const glm::vec3* scl = scales.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        // Build TRS matrix: T * R * S
        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos[i]);
        glm::mat4 R = glm::toMat4(rot[i]);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scl[i]);
        xform[i] = T * R * S;
    }
}

void computeTransformsUniformScale(
    Span<glm::mat4> transforms,
    Span<const glm::vec3> positions,
    Span<const glm::quat> rotations,
    Span<const float> scales,
    size_t count
) {
    glm::mat4* xform = transforms.ptr();
    const glm::vec3* pos = positions.ptr();
    const glm::quat* rot = rotations.ptr();
    const float* scl = scales.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), pos[i]);
        glm::mat4 R = glm::toMat4(rot[i]);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scl[i]));
        xform[i] = T * R * S;
    }
}

void updateTransformPositions(
    Span<glm::mat4> transforms,
    Span<const glm::vec3> positions,
    size_t count
) {
    glm::mat4* xform = transforms.ptr();
    const glm::vec3* pos = positions.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        xform[i][3] = glm::vec4(pos[i], 1.0f);
    }
}

// ============================================================================
// Frustum Culling
// ============================================================================

Frustum Frustum::fromViewProj(const glm::mat4& vp) {
    Frustum f;
    
    // Extract planes using Gribb/Hartmann method
    // Left plane
    f.planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );
    
    // Right plane
    f.planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );
    
    // Bottom plane
    f.planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );
    
    // Top plane
    f.planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );
    
    // Near plane
    f.planes[4] = glm::vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );
    
    // Far plane
    f.planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );
    
    // Normalize all planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(f.planes[i]));
        if (len > 0.0f) {
            f.planes[i] /= len;
        }
    }
    
    return f;
}

// Test if AABB is inside or intersecting frustum
static bool testAABBFrustum(const AABB& aabb, const Frustum& frustum) {
    for (int i = 0; i < 6; ++i) {
        const glm::vec4& plane = frustum.planes[i];
        glm::vec3 n(plane);
        
        // Find the positive vertex (furthest along plane normal)
        glm::vec3 pVertex(
            (n.x >= 0) ? aabb.max.x : aabb.min.x,
            (n.y >= 0) ? aabb.max.y : aabb.min.y,
            (n.z >= 0) ? aabb.max.z : aabb.min.z
        );
        
        // If positive vertex is outside, AABB is outside
        if (glm::dot(n, pVertex) + plane.w < 0) {
            return false;
        }
    }
    return true;
}

void frustumCull(
    Span<uint32_t> visibility,
    Span<const AABB> bounds,
    const Frustum& frustum,
    size_t count
) {
    uint32_t* vis = visibility.ptr();
    const AABB* aabb = bounds.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        vis[i] = testAABBFrustum(aabb[i], frustum) ? 1 : 0;
    }
}

void frustumCullTransformed(
    Span<uint32_t> visibility,
    Span<const AABB> bounds,
    Span<const glm::mat4> transforms,
    const Frustum& frustum,
    size_t count
) {
    uint32_t* vis = visibility.ptr();
    const AABB* localBounds = bounds.ptr();
    const glm::mat4* xform = transforms.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        // Transform AABB to world space (conservative approximation)
        const AABB& local = localBounds[i];
        const glm::mat4& m = xform[i];
        
        glm::vec3 center = local.center();
        glm::vec3 extents = local.extents();
        
        // Transform center
        glm::vec3 worldCenter = glm::vec3(m * glm::vec4(center, 1.0f));
        
        // Transform extents (conservative)
        glm::vec3 worldExtents(
            std::abs(m[0][0]) * extents.x + std::abs(m[1][0]) * extents.y + std::abs(m[2][0]) * extents.z,
            std::abs(m[0][1]) * extents.x + std::abs(m[1][1]) * extents.y + std::abs(m[2][1]) * extents.z,
            std::abs(m[0][2]) * extents.x + std::abs(m[1][2]) * extents.y + std::abs(m[2][2]) * extents.z
        );
        
        AABB worldAABB{
            worldCenter - worldExtents,
            worldCenter + worldExtents
        };
        
        vis[i] = testAABBFrustum(worldAABB, frustum) ? 1 : 0;
    }
}

uint64_t frustumCullBatch64(
    Span<const AABB> bounds,
    const Frustum& frustum,
    size_t count
) {
    if (count > 64) count = 64;
    
    uint64_t mask = 0;
    const AABB* aabb = bounds.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        if (testAABBFrustum(aabb[i], frustum)) {
            mask |= (1ULL << i);
        }
    }
    
    return mask;
}

// ============================================================================
// Distance/Sorting
// ============================================================================

void computeDistancesSq(
    Span<float> distances,
    Span<const glm::vec3> positions,
    const glm::vec3& origin,
    size_t count
) {
    float* dist = distances.ptr();
    const glm::vec3* pos = positions.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        glm::vec3 delta = pos[i] - origin;
        dist[i] = glm::dot(delta, delta);
    }
}

void sortByDistance(
    Span<uint32_t> indices,
    Span<const float> distances,
    size_t count,
    bool ascending
) {
    uint32_t* idx = indices.ptr();
    const float* dist = distances.ptr();
    
    // Initialize indices
    for (size_t i = 0; i < count; ++i) {
        idx[i] = static_cast<uint32_t>(i);
    }
    
    // Sort
    if (ascending) {
        std::sort(idx, idx + count, [dist](uint32_t a, uint32_t b) {
            return dist[a] < dist[b];
        });
    } else {
        std::sort(idx, idx + count, [dist](uint32_t a, uint32_t b) {
            return dist[a] > dist[b];
        });
    }
}

// ============================================================================
// Batch Operations
// ============================================================================

void copyVec3(
    Span<glm::vec3> dst,
    Span<const glm::vec3> src,
    size_t count
) {
    std::memcpy(dst.ptr(), src.ptr(), count * sizeof(glm::vec3));
}

void copyMat4(
    Span<glm::mat4> dst,
    Span<const glm::mat4> src,
    size_t count
) {
    std::memcpy(dst.ptr(), src.ptr(), count * sizeof(glm::mat4));
}

void zeroFill(
    Span<float> data,
    size_t count
) {
    std::memset(data.ptr(), 0, count * sizeof(float));
}

void fillVec3(
    Span<glm::vec3> data,
    const glm::vec3& value,
    size_t count
) {
    glm::vec3* ptr = data.ptr();
    for (size_t i = 0; i < count; ++i) {
        ptr[i] = value;
    }
}

// ============================================================================
// Physics Helpers
// ============================================================================

void applyForces(
    Span<glm::vec3> velocities,
    Span<const glm::vec3> forces,
    Span<const float> inverseMasses,
    float dt,
    size_t count
) {
    glm::vec3* vel = velocities.ptr();
    const glm::vec3* force = forces.ptr();
    const float* invMass = inverseMasses.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        vel[i] += force[i] * invMass[i] * dt;
    }
}

void applyGravity(
    Span<glm::vec3> velocities,
    const glm::vec3& gravity,
    float dt,
    size_t count
) {
    glm::vec3* vel = velocities.ptr();
    glm::vec3 gravityDt = gravity * dt;
    
    for (size_t i = 0; i < count; ++i) {
        vel[i] += gravityDt;
    }
}

void computeAABBs(
    Span<AABB> aabbs,
    Span<const glm::vec3> positions,
    Span<const glm::vec3> halfExtents,
    size_t count
) {
    AABB* aabb = aabbs.ptr();
    const glm::vec3* pos = positions.ptr();
    const glm::vec3* ext = halfExtents.ptr();
    
    for (size_t i = 0; i < count; ++i) {
        aabb[i].min = pos[i] - ext[i];
        aabb[i].max = pos[i] + ext[i];
    }
}

} // namespace jupiter::ecs::simd

