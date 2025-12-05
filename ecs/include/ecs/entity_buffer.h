#pragma once

/**
 * @file entity_buffer.h
 * @brief EntityBuffer - Columnar storage for all entity data
 * 
 * One side of the double-buffer system. Contains all columns needed
 * for transform, physics, and rendering data.
 */

#include "types.h"
#include "column_storage.h"
#include "simd_ops.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jupiter::ecs {

/**
 * @brief Complete columnar storage for entity data
 * 
 * Contains all component columns in SOA (Structure of Arrays) layout
 * for cache-efficient batch processing.
 */
struct EntityBuffer {
    // Generation counter for clone-on-write optimization
    uint64_t generation = 0;
    
    // Active entity count
    uint32_t count = 0;
    
    // Maximum capacity
    uint32_t capacity = 0;

    // ========================================================================
    // Entity Metadata
    // ========================================================================
    Column<EntityId> ids;
    Column<EntityFlags> flags;

    // ========================================================================
    // Transform Columns
    // ========================================================================
    Column<glm::vec3> positions;
    Column<glm::quat> rotations;
    Column<glm::vec3> scales;
    Column<glm::mat4> transforms;  // Cached model matrices

    // ========================================================================
    // Physics Columns
    // ========================================================================
    Column<glm::vec3> linearVelocities;
    Column<glm::vec3> angularVelocities;
    Column<float> massesInverse;
    Column<glm::vec3> inertiasInverse;
    Column<glm::vec3> forceAccumulators;
    Column<glm::vec3> torqueAccumulators;

    // Physics material
    Column<float> linearDamping;
    Column<float> angularDamping;
    Column<float> restitution;
    Column<float> friction;

    // Sleep system
    Column<uint8_t> isAwake;
    Column<uint8_t> sleepCounters;
    Column<float> motionEnergy;

    // Collision
    Column<float> sphereRadii;
    Column<glm::vec3> boxHalfExtents;
    Column<simd::AABB> aabbs;

    // ========================================================================
    // Rendering Columns
    // ========================================================================
    Column<uint32_t> materialIds;
    Column<uint32_t> meshIds;
    Column<uint32_t> visibility;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /**
     * @brief Initialize all columns with given capacity
     */
    void init(uint32_t cap) {
        capacity = cap;
        count = 0;
        generation = 0;

        // Reserve space in all columns
        ids.reserve(cap);
        flags.reserve(cap);
        
        positions.reserve(cap);
        rotations.reserve(cap);
        scales.reserve(cap);
        transforms.reserve(cap);
        
        linearVelocities.reserve(cap);
        angularVelocities.reserve(cap);
        massesInverse.reserve(cap);
        inertiasInverse.reserve(cap);
        forceAccumulators.reserve(cap);
        torqueAccumulators.reserve(cap);
        
        linearDamping.reserve(cap);
        angularDamping.reserve(cap);
        restitution.reserve(cap);
        friction.reserve(cap);
        
        isAwake.reserve(cap);
        sleepCounters.reserve(cap);
        motionEnergy.reserve(cap);
        
        sphereRadii.reserve(cap);
        boxHalfExtents.reserve(cap);
        aabbs.reserve(cap);
        
        materialIds.reserve(cap);
        meshIds.reserve(cap);
        visibility.reserve(cap);
    }

    /**
     * @brief Resize all columns to the specified count
     */
    void resize(uint32_t newCount) {
        count = newCount;
        
        ids.resize(newCount);
        flags.resize(newCount);
        
        positions.resize(newCount);
        rotations.resize(newCount);
        scales.resize(newCount);
        transforms.resize(newCount);
        
        linearVelocities.resize(newCount);
        angularVelocities.resize(newCount);
        massesInverse.resize(newCount);
        inertiasInverse.resize(newCount);
        forceAccumulators.resize(newCount);
        torqueAccumulators.resize(newCount);
        
        linearDamping.resize(newCount);
        angularDamping.resize(newCount);
        restitution.resize(newCount);
        friction.resize(newCount);
        
        isAwake.resize(newCount);
        sleepCounters.resize(newCount);
        motionEnergy.resize(newCount);
        
        sphereRadii.resize(newCount);
        boxHalfExtents.resize(newCount);
        aabbs.resize(newCount);
        
        materialIds.resize(newCount);
        meshIds.resize(newCount);
        visibility.resize(newCount);
    }

    /**
     * @brief Clear all columns
     */
    void clear() {
        count = 0;
        
        ids.clear();
        flags.clear();
        
        positions.clear();
        rotations.clear();
        scales.clear();
        transforms.clear();
        
        linearVelocities.clear();
        angularVelocities.clear();
        massesInverse.clear();
        inertiasInverse.clear();
        forceAccumulators.clear();
        torqueAccumulators.clear();
        
        linearDamping.clear();
        angularDamping.clear();
        restitution.clear();
        friction.clear();
        
        isAwake.clear();
        sleepCounters.clear();
        motionEnergy.clear();
        
        sphereRadii.clear();
        boxHalfExtents.clear();
        aabbs.clear();
        
        materialIds.clear();
        meshIds.clear();
        visibility.clear();
    }

    /**
     * @brief Copy specified column from another buffer (for clone-on-write)
     */
    void cloneColumn(ColumnId col, const EntityBuffer& src) {
        switch (col) {
            case ColumnId::Ids: ids = src.ids; break;
            case ColumnId::Flags: flags = src.flags; break;
            case ColumnId::Positions: positions = src.positions; break;
            case ColumnId::Rotations: rotations = src.rotations; break;
            case ColumnId::Scales: scales = src.scales; break;
            case ColumnId::Transforms: transforms = src.transforms; break;
            case ColumnId::LinearVelocity: linearVelocities = src.linearVelocities; break;
            case ColumnId::AngularVelocity: angularVelocities = src.angularVelocities; break;
            case ColumnId::MassInverse: massesInverse = src.massesInverse; break;
            case ColumnId::InertiaInverse: inertiasInverse = src.inertiasInverse; break;
            case ColumnId::ForceAccum: forceAccumulators = src.forceAccumulators; break;
            case ColumnId::TorqueAccum: torqueAccumulators = src.torqueAccumulators; break;
            case ColumnId::LinearDamping: linearDamping = src.linearDamping; break;
            case ColumnId::AngularDamping: angularDamping = src.angularDamping; break;
            case ColumnId::Restitution: restitution = src.restitution; break;
            case ColumnId::Friction: friction = src.friction; break;
            case ColumnId::IsAwake: isAwake = src.isAwake; break;
            case ColumnId::SleepCounter: sleepCounters = src.sleepCounters; break;
            case ColumnId::MotionEnergy: motionEnergy = src.motionEnergy; break;
            case ColumnId::SphereRadii: sphereRadii = src.sphereRadii; break;
            case ColumnId::BoxHalfExtents: boxHalfExtents = src.boxHalfExtents; break;
            case ColumnId::AABBs: aabbs = src.aabbs; break;
            case ColumnId::MaterialIds: materialIds = src.materialIds; break;
            case ColumnId::MeshIds: meshIds = src.meshIds; break;
            case ColumnId::Visibility: visibility = src.visibility; break;
            default: break;
        }
    }
};

} // namespace jupiter::ecs

