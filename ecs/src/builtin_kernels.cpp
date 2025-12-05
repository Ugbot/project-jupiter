/**
 * @file builtin_kernels.cpp
 * @brief Implementation of built-in ECS kernels
 */

#define GLM_ENABLE_EXPERIMENTAL
#include "ecs/builtin_kernels.h"
#include "ecs/kernel_registry.h"
#include "ecs/simd_ops.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace jupiter::ecs::kernels {

// ============================================================================
// Transform Kernels
// ============================================================================

Status transformKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    auto positions = input->column<const glm::vec3>(ColumnId::Positions);
    auto rotations = input->column<const glm::quat>(ColumnId::Rotations);
    auto scales = input->column<const glm::vec3>(ColumnId::Scales);
    auto transforms = output->column<glm::mat4>(ColumnId::Transforms);

    if (!positions.valid() || !rotations.valid() || !scales.valid() || !transforms.valid()) {
        return Status::InvalidArgument;
    }

    simd::computeTransforms(transforms, positions, rotations, scales, input->length);
    return Status::Ok;
}

Status updatePositionsKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    auto positions = input->column<const glm::vec3>(ColumnId::Positions);
    auto transforms = output->column<glm::mat4>(ColumnId::Transforms);

    if (!positions.valid() || !transforms.valid()) {
        return Status::InvalidArgument;
    }

    simd::updateTransformPositions(transforms, positions, input->length);
    return Status::Ok;
}

// ============================================================================
// Physics Kernels
// ============================================================================

Status physicsIntegrateKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    // Get delta time from context
    const KernelContext* ctx = static_cast<const KernelContext*>(input->userData);
    if (!ctx) {
        return Status::InvalidArgument;
    }
    float dt = ctx->deltaTime;

    auto positions = input->column<glm::vec3>(ColumnId::Positions);
    auto velocities = input->column<const glm::vec3>(ColumnId::LinearVelocity);
    auto outPositions = output->column<glm::vec3>(ColumnId::Positions);

    if (!positions.valid() || !velocities.valid() || !outPositions.valid()) {
        return Status::InvalidArgument;
    }

    // Copy positions to output if different buffers
    if (positions.ptr() != outPositions.ptr()) {
        simd::copyVec3(outPositions, positions, input->length);
    }

    simd::integratePositions(outPositions, velocities, dt, input->length);
    return Status::Ok;
}

Status physicsRotateKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    const KernelContext* ctx = static_cast<const KernelContext*>(input->userData);
    if (!ctx) {
        return Status::InvalidArgument;
    }
    float dt = ctx->deltaTime;

    auto rotations = output->column<glm::quat>(ColumnId::Rotations);
    auto angularVelocities = input->column<const glm::vec3>(ColumnId::AngularVelocity);

    if (!rotations.valid() || !angularVelocities.valid()) {
        return Status::InvalidArgument;
    }

    simd::integrateRotations(rotations, angularVelocities, dt, input->length);
    return Status::Ok;
}

Status applyForcesKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    const KernelContext* ctx = static_cast<const KernelContext*>(input->userData);
    if (!ctx) {
        return Status::InvalidArgument;
    }
    float dt = ctx->deltaTime;

    auto velocities = output->column<glm::vec3>(ColumnId::LinearVelocity);
    auto forces = input->column<const glm::vec3>(ColumnId::ForceAccum);
    auto massesInverse = input->column<const float>(ColumnId::MassInverse);

    if (!velocities.valid() || !forces.valid() || !massesInverse.valid()) {
        return Status::InvalidArgument;
    }

    simd::applyForces(velocities, forces, massesInverse, dt, input->length);
    return Status::Ok;
}

Status gravityKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    const KernelContext* ctx = static_cast<const KernelContext*>(input->userData);
    if (!ctx) {
        return Status::InvalidArgument;
    }
    float dt = ctx->deltaTime;

    // Gravity vector from user context (default to Earth gravity if not specified)
    glm::vec3 gravity(0.0f, -9.81f, 0.0f);
    if (ctx->userContext) {
        gravity = *static_cast<const glm::vec3*>(ctx->userContext);
    }

    auto velocities = output->column<glm::vec3>(ColumnId::LinearVelocity);
    auto massesInverse = input->column<const float>(ColumnId::MassInverse);

    if (!velocities.valid()) {
        return Status::InvalidArgument;
    }

    // Apply gravity only to dynamic objects (massInverse > 0)
    glm::vec3* vel = velocities.ptr();
    const float* invMass = massesInverse.valid() ? massesInverse.ptr() : nullptr;
    glm::vec3 gravityDt = gravity * dt;

    for (size_t i = 0; i < input->length; ++i) {
        // Only apply to dynamic objects
        if (!invMass || invMass[i] > 0.0f) {
            vel[i] += gravityDt;
        }
    }

    return Status::Ok;
}

Status clearForcesKernel(const ExecBatch* input, ExecBatch* output) {
    if (!output || output->length == 0) {
        return Status::Ok;
    }

    auto forces = output->column<glm::vec3>(ColumnId::ForceAccum);
    auto torques = output->column<glm::vec3>(ColumnId::TorqueAccum);

    if (forces.valid()) {
        simd::fillVec3(forces, glm::vec3(0.0f), output->length);
    }

    if (torques.valid()) {
        simd::fillVec3(torques, glm::vec3(0.0f), output->length);
    }

    return Status::Ok;
}

// ============================================================================
// Culling Kernels
// ============================================================================

Status computeAABBsKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    auto positions = input->column<const glm::vec3>(ColumnId::Positions);
    auto halfExtents = input->column<const glm::vec3>(ColumnId::BoxHalfExtents);
    auto aabbs = output->column<simd::AABB>(ColumnId::AABBs);

    if (!positions.valid() || !halfExtents.valid() || !aabbs.valid()) {
        return Status::InvalidArgument;
    }

    simd::computeAABBs(aabbs, positions, halfExtents, input->length);
    return Status::Ok;
}

Status frustumCullKernel(const ExecBatch* input, ExecBatch* output) {
    if (!input || !output || input->length == 0) {
        return Status::Ok;
    }

    // Frustum should be passed via userData
    const simd::Frustum* frustum = static_cast<const simd::Frustum*>(input->userData);
    if (!frustum) {
        // No frustum - mark all visible
        auto visibility = output->column<uint32_t>(ColumnId::Visibility);
        if (visibility.valid()) {
            for (size_t i = 0; i < input->length; ++i) {
                visibility[i] = 1;
            }
        }
        return Status::Ok;
    }

    auto aabbs = input->column<const simd::AABB>(ColumnId::AABBs);
    auto transforms = input->column<const glm::mat4>(ColumnId::Transforms);
    auto visibility = output->column<uint32_t>(ColumnId::Visibility);

    if (!visibility.valid()) {
        return Status::InvalidArgument;
    }

    if (aabbs.valid() && transforms.valid()) {
        // Use transformed AABBs
        simd::frustumCullTransformed(visibility, aabbs, transforms, *frustum, input->length);
    } else if (aabbs.valid()) {
        // Use AABBs directly (already in world space)
        simd::frustumCull(visibility, aabbs, *frustum, input->length);
    } else {
        // No bounds - mark all visible
        for (size_t i = 0; i < input->length; ++i) {
            visibility[i] = 1;
        }
    }

    return Status::Ok;
}

// ============================================================================
// Kernel Definitions
// ============================================================================

Kernel getTransformKernel() {
    return KernelBuilder("transform")
        .exec(transformKernel)
        .inputs(ColumnId::Positions | ColumnId::Rotations | ColumnId::Scales)
        .outputs(ColumnId::Transforms)
        .mode(KernelMode::ReadWrite)
        .priority(100)  // Run after physics
        .build();
}

Kernel getPhysicsIntegrateKernel() {
    return KernelBuilder("physics_integrate")
        .exec(physicsIntegrateKernel)
        .inputs(ColumnId::Positions | ColumnId::LinearVelocity)
        .outputs(ColumnId::Positions)
        .mode(KernelMode::ReadWrite)
        .priority(10)  // Run early
        .build();
}

Kernel getFrustumCullKernel() {
    return KernelBuilder("frustum_cull")
        .exec(frustumCullKernel)
        .inputs(ColumnId::AABBs | ColumnId::Transforms)
        .outputs(ColumnId::Visibility)
        .readOnly()  // Can run on reader threads
        .priority(200)  // Run after transforms
        .build();
}

Kernel getGravityKernel() {
    return KernelBuilder("gravity")
        .exec(gravityKernel)
        .inputs(ColumnId::LinearVelocity | ColumnId::MassInverse)
        .outputs(ColumnId::LinearVelocity)
        .mode(KernelMode::ReadWrite)
        .priority(5)  // Run before integration
        .build();
}

// ============================================================================
// Registration
// ============================================================================

void registerBuiltinKernels() {
    KernelRegistry& registry = KernelRegistry::instance();

    // Transform
    registry.registerKernel("transform", getTransformKernel());
    registry.registerKernel("update_positions", 
        KernelBuilder("update_positions")
            .exec(updatePositionsKernel)
            .inputs(ColumnId::Positions | ColumnId::Transforms)
            .outputs(ColumnId::Transforms)
            .mode(KernelMode::ReadWrite)
            .priority(101)
            .build());

    // Physics
    registry.registerKernel("physics_integrate", getPhysicsIntegrateKernel());
    registry.registerKernel("physics_rotate",
        KernelBuilder("physics_rotate")
            .exec(physicsRotateKernel)
            .inputs(ColumnId::Rotations | ColumnId::AngularVelocity)
            .outputs(ColumnId::Rotations)
            .mode(KernelMode::ReadWrite)
            .priority(11)
            .build());
    registry.registerKernel("apply_forces",
        KernelBuilder("apply_forces")
            .exec(applyForcesKernel)
            .inputs(ColumnId::LinearVelocity | ColumnId::ForceAccum | ColumnId::MassInverse)
            .outputs(ColumnId::LinearVelocity)
            .mode(KernelMode::ReadWrite)
            .priority(8)
            .build());
    registry.registerKernel("gravity", getGravityKernel());
    registry.registerKernel("clear_forces",
        KernelBuilder("clear_forces")
            .exec(clearForcesKernel)
            .inputs(ColumnId::ForceAccum | ColumnId::TorqueAccum)
            .outputs(ColumnId::ForceAccum | ColumnId::TorqueAccum)
            .mode(KernelMode::ReadWrite)
            .priority(1)  // Run first
            .build());

    // Culling
    registry.registerKernel("compute_aabbs",
        KernelBuilder("compute_aabbs")
            .exec(computeAABBsKernel)
            .inputs(ColumnId::Positions | ColumnId::BoxHalfExtents)
            .outputs(ColumnId::AABBs)
            .mode(KernelMode::ReadWrite)
            .priority(150)
            .build());
    registry.registerKernel("frustum_cull", getFrustumCullKernel());
}

} // namespace jupiter::ecs::kernels

