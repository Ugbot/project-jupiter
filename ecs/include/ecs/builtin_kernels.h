#pragma once

/**
 * @file builtin_kernels.h
 * @brief Built-in ECS kernels for common operations
 * 
 * Provides vectorized kernels for:
 * - Transform computation (TRS -> mat4)
 * - Physics integration (velocity -> position)
 * - Frustum culling (AABB vs view frustum)
 */

#include "types.h"
#include "kernel.h"
#include "simd_ops.h"

namespace jupiter::ecs::kernels {

// ============================================================================
// Transform Kernels
// ============================================================================

/**
 * @brief Compute model matrices from position/rotation/scale
 * 
 * Input: Positions, Rotations, Scales
 * Output: Transforms
 */
Status transformKernel(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Update only translation component of transforms
 * 
 * Input: Positions, Transforms (existing)
 * Output: Transforms (with updated position)
 */
Status updatePositionsKernel(const ExecBatch* input, ExecBatch* output);

// ============================================================================
// Physics Kernels
// ============================================================================

/**
 * @brief Integrate linear velocities into positions
 * 
 * Input: Positions, LinearVelocity
 * Output: Positions
 * UserData: KernelContext* (for deltaTime)
 */
Status physicsIntegrateKernel(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Integrate angular velocities into rotations
 * 
 * Input: Rotations, AngularVelocity
 * Output: Rotations
 * UserData: KernelContext* (for deltaTime)
 */
Status physicsRotateKernel(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Apply forces to velocities (F = ma)
 * 
 * Input: LinearVelocity, ForceAccum, MassInverse
 * Output: LinearVelocity
 * UserData: KernelContext* (for deltaTime)
 */
Status applyForcesKernel(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Apply gravity to all entities
 * 
 * Input: LinearVelocity, MassInverse
 * Output: LinearVelocity
 * UserData: KernelContext* (for deltaTime, gravity in userContext)
 */
Status gravityKernel(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Clear force accumulators
 * 
 * Input: ForceAccum, TorqueAccum
 * Output: ForceAccum, TorqueAccum (zeroed)
 */
Status clearForcesKernel(const ExecBatch* input, ExecBatch* output);

// ============================================================================
// Culling Kernels
// ============================================================================

/**
 * @brief Compute AABBs from positions and half-extents
 * 
 * Input: Positions, BoxHalfExtents
 * Output: AABBs
 */
Status computeAABBsKernel(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Frustum cull AABBs against view frustum
 * 
 * Input: AABBs, Transforms (for world-space AABB)
 * Output: Visibility
 * UserData: simd::Frustum* (view frustum)
 * 
 * Mode: ReadOnly (can run on reader threads)
 */
Status frustumCullKernel(const ExecBatch* input, ExecBatch* output);

// ============================================================================
// Kernel Definitions
// ============================================================================

/**
 * @brief Get the transform kernel definition
 */
Kernel getTransformKernel();

/**
 * @brief Get the physics integration kernel definition
 */
Kernel getPhysicsIntegrateKernel();

/**
 * @brief Get the frustum cull kernel definition
 */
Kernel getFrustumCullKernel();

/**
 * @brief Get the gravity kernel definition
 */
Kernel getGravityKernel();

// ============================================================================
// Registration
// ============================================================================

/**
 * @brief Register all built-in kernels with the registry
 * 
 * Call once at startup to make kernels available via KernelRegistry.
 */
void registerBuiltinKernels();

} // namespace jupiter::ecs::kernels

