/**
 * @file world.cpp
 * @brief Double-buffered World implementation
 */

#define GLM_ENABLE_EXPERIMENTAL
#include "ecs/world.h"
#include "ecs/simd_ops.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cassert>

namespace jupiter::ecs {

// ============================================================================
// Construction
// ============================================================================

World::World(uint32_t capacity) {
    buffers_[0].init(capacity);
    buffers_[1].init(capacity);
    
    freeIndices_.reserve(capacity);
}

// ============================================================================
// Reader API
// ============================================================================

ReadSnapshot World::acquireReadSnapshot() const noexcept {
    uint8_t idx = readIdx_.load(std::memory_order_acquire);
    return ReadSnapshot(&buffers_[idx], generation_.load(std::memory_order_acquire));
}

// ============================================================================
// Writer API
// ============================================================================

void World::markDirty(ColumnId cols) noexcept {
    uint32_t colBits = static_cast<uint32_t>(cols);
    uint32_t prev = dirtyMask_.fetch_or(colBits, std::memory_order_acq_rel);
    
    // Clone-on-write: for each newly dirty column, clone if generations differ
    uint8_t readIdx = readIdx_.load(std::memory_order_acquire);
    const EntityBuffer& src = buffers_[readIdx];
    EntityBuffer& dst = buffers_[readIdx ^ 1];
    
    if (src.generation != dst.generation) {
        // Find newly dirty columns (weren't dirty before)
        uint32_t newlyDirty = colBits & ~prev;
        
        // Clone each newly dirty column
        for (uint32_t bit = 1; newlyDirty != 0; bit <<= 1) {
            if (newlyDirty & bit) {
                dst.cloneColumn(static_cast<ColumnId>(bit), src);
                newlyDirty &= ~bit;
            }
        }
    }
}

void World::swap() noexcept {
    uint8_t oldReadIdx = readIdx_.load(std::memory_order_acquire);
    uint8_t newReadIdx = oldReadIdx ^ 1;
    
    // Copy entity count to new read buffer (was write buffer)
    buffers_[newReadIdx].count = buffers_[oldReadIdx].count;
    
    // Increment generation counter for new write buffer
    buffers_[oldReadIdx].generation = generation_.load(std::memory_order_acquire) + 1;
    
    // Atomically flip read index
    readIdx_.store(newReadIdx, std::memory_order_release);
    
    // Increment global generation
    generation_.fetch_add(1, std::memory_order_release);
    
    // Clear dirty mask
    dirtyMask_.store(0, std::memory_order_release);
}

// ============================================================================
// Entity Management
// ============================================================================

uint32_t World::allocateIndex() {
    if (!freeIndices_.empty()) {
        uint32_t index = freeIndices_.back();
        freeIndices_.pop_back();
        return index;
    }
    
    // No free indices, use next available
    uint8_t readIdx = readIdx_.load(std::memory_order_acquire);
    uint32_t count = buffers_[readIdx].count;
    
    if (count < buffers_[readIdx].capacity) {
        return count;
    }
    
    // Out of capacity
    return UINT32_MAX;
}

void World::freeIndex(uint32_t index) {
    freeIndices_.push_back(index);
}

void World::createAt(uint32_t index, const EntityCreateInfo& info) {
    // Create in both buffers for stability
    for (int b = 0; b < 2; ++b) {
        EntityBuffer& buf = buffers_[b];
        
        // Ensure buffer is large enough
        if (index >= buf.ids.size()) {
            buf.resize(index + 1);
        }
        
        // Assign ID
        EntityId id = (info.id != INVALID_ENTITY) ? info.id : nextEntityId_++;
        buf.ids[index] = id;
        buf.flags[index] = info.flags;
        
        // Transform
        buf.positions[index] = info.position;
        buf.rotations[index] = info.rotation;
        buf.scales[index] = info.scale;
        
        // Compute initial transform matrix
        glm::mat4 T = glm::translate(glm::mat4(1.0f), info.position);
        glm::mat4 R = glm::toMat4(info.rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), info.scale);
        buf.transforms[index] = T * R * S;
        
        // Physics
        buf.linearVelocities[index] = info.linearVelocity;
        buf.angularVelocities[index] = info.angularVelocity;
        buf.massesInverse[index] = (info.mass > 0.0f) ? (1.0f / info.mass) : 0.0f;
        buf.inertiasInverse[index] = glm::vec3(0.0f);
        buf.forceAccumulators[index] = glm::vec3(0.0f);
        buf.torqueAccumulators[index] = glm::vec3(0.0f);
        
        // Physics material defaults
        buf.linearDamping[index] = 0.99f;
        buf.angularDamping[index] = 0.99f;
        buf.restitution[index] = 0.5f;
        buf.friction[index] = 0.5f;
        
        // Sleep system
        buf.isAwake[index] = 1;
        buf.sleepCounters[index] = 0;
        buf.motionEnergy[index] = 1.0f;
        
        // Collision (no shape by default)
        buf.sphereRadii[index] = -1.0f;
        buf.boxHalfExtents[index] = glm::vec3(-1.0f);
        buf.aabbs[index] = simd::AABB{glm::vec3(0.0f), glm::vec3(0.0f)};
        
        // Rendering
        buf.materialIds[index] = info.materialId;
        buf.meshIds[index] = info.meshId;
        buf.visibility[index] = 1;  // Visible by default
        
        // Update count if needed
        if (index >= buf.count) {
            buf.count = index + 1;
        }
    }
}

uint32_t World::create(const EntityCreateInfo& info) {
    uint32_t index = allocateIndex();
    if (index == UINT32_MAX) {
        return UINT32_MAX;
    }
    
    createAt(index, info);
    return index;
}

void World::destroy(uint32_t index) {
    uint8_t readIdx = readIdx_.load(std::memory_order_acquire);
    if (index >= buffers_[readIdx].count) {
        return;  // Invalid index
    }
    
    // Mark as inactive in both buffers
    for (int b = 0; b < 2; ++b) {
        buffers_[b].ids[index] = INVALID_ENTITY;
        buffers_[b].flags[index] = EntityFlags::None;
    }
    
    // Add to free list
    freeIndex(index);
}

void World::queueCreate(const EntityCreateInfo& info) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    DeferredOp op;
    op.type = DeferredOpType::Create;
    op.createInfo = info;
    opQueue_.push(op);
}

void World::queueDestroy(uint32_t index) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    DeferredOp op;
    op.type = DeferredOpType::Destroy;
    op.destroyIndex = index;
    opQueue_.push(op);
}

void World::processQueued() {
    std::queue<DeferredOp> localQueue;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::swap(localQueue, opQueue_);
    }
    
    while (!localQueue.empty()) {
        DeferredOp& op = localQueue.front();
        
        switch (op.type) {
            case DeferredOpType::Create:
                create(op.createInfo);
                break;
            case DeferredOpType::Destroy:
                destroy(op.destroyIndex);
                break;
        }
        
        localQueue.pop();
    }
}

// ============================================================================
// Batch Creation
// ============================================================================

ExecBatch World::makeReadBatch(size_t offset, size_t length) const noexcept {
    uint8_t idx = readIdx_.load(std::memory_order_acquire);
    const EntityBuffer& buf = buffers_[idx];
    
    if (offset >= buf.count) {
        return ExecBatch{};
    }
    
    length = std::min(length, static_cast<size_t>(buf.count) - offset);
    
    ExecBatch batch;
    batch.length = length;
    batch.offset = offset;
    
    // Set up column pointers (const-cast safe for read-only use)
    batch.setColumn(ColumnId::Ids, const_cast<EntityId*>(buf.ids.data() + offset));
    batch.setColumn(ColumnId::Flags, const_cast<EntityFlags*>(buf.flags.data() + offset));
    batch.setColumn(ColumnId::Positions, const_cast<glm::vec3*>(buf.positions.data() + offset));
    batch.setColumn(ColumnId::Rotations, const_cast<glm::quat*>(buf.rotations.data() + offset));
    batch.setColumn(ColumnId::Scales, const_cast<glm::vec3*>(buf.scales.data() + offset));
    batch.setColumn(ColumnId::Transforms, const_cast<glm::mat4*>(buf.transforms.data() + offset));
    batch.setColumn(ColumnId::LinearVelocity, const_cast<glm::vec3*>(buf.linearVelocities.data() + offset));
    batch.setColumn(ColumnId::AngularVelocity, const_cast<glm::vec3*>(buf.angularVelocities.data() + offset));
    batch.setColumn(ColumnId::MassInverse, const_cast<float*>(buf.massesInverse.data() + offset));
    batch.setColumn(ColumnId::MaterialIds, const_cast<uint32_t*>(buf.materialIds.data() + offset));
    batch.setColumn(ColumnId::MeshIds, const_cast<uint32_t*>(buf.meshIds.data() + offset));
    batch.setColumn(ColumnId::Visibility, const_cast<uint32_t*>(buf.visibility.data() + offset));
    batch.setColumn(ColumnId::AABBs, const_cast<simd::AABB*>(buf.aabbs.data() + offset));
    
    return batch;
}

ExecBatch World::makeWriteBatch(size_t offset, size_t length) noexcept {
    EntityBuffer& buf = writeBuffer();
    
    if (offset >= buf.count) {
        return ExecBatch{};
    }
    
    length = std::min(length, static_cast<size_t>(buf.count) - offset);
    
    ExecBatch batch;
    batch.length = length;
    batch.offset = offset;
    
    // Set up column pointers
    batch.setColumn(ColumnId::Ids, buf.ids.data() + offset);
    batch.setColumn(ColumnId::Flags, buf.flags.data() + offset);
    batch.setColumn(ColumnId::Positions, buf.positions.data() + offset);
    batch.setColumn(ColumnId::Rotations, buf.rotations.data() + offset);
    batch.setColumn(ColumnId::Scales, buf.scales.data() + offset);
    batch.setColumn(ColumnId::Transforms, buf.transforms.data() + offset);
    batch.setColumn(ColumnId::LinearVelocity, buf.linearVelocities.data() + offset);
    batch.setColumn(ColumnId::AngularVelocity, buf.angularVelocities.data() + offset);
    batch.setColumn(ColumnId::MassInverse, buf.massesInverse.data() + offset);
    batch.setColumn(ColumnId::ForceAccum, buf.forceAccumulators.data() + offset);
    batch.setColumn(ColumnId::TorqueAccum, buf.torqueAccumulators.data() + offset);
    batch.setColumn(ColumnId::MaterialIds, buf.materialIds.data() + offset);
    batch.setColumn(ColumnId::MeshIds, buf.meshIds.data() + offset);
    batch.setColumn(ColumnId::Visibility, buf.visibility.data() + offset);
    batch.setColumn(ColumnId::AABBs, buf.aabbs.data() + offset);
    
    return batch;
}

// ============================================================================
// Capacity
// ============================================================================

void World::reserve(uint32_t newCapacity) {
    if (newCapacity <= buffers_[0].capacity) {
        return;
    }
    
    for (int b = 0; b < 2; ++b) {
        buffers_[b].init(newCapacity);
    }
    
    freeIndices_.reserve(newCapacity);
}

// ============================================================================
// Clone Column (for clone-on-write)
// ============================================================================

void World::cloneColumn(ColumnId col) {
    uint8_t readIdx = readIdx_.load(std::memory_order_acquire);
    const EntityBuffer& src = buffers_[readIdx];
    EntityBuffer& dst = buffers_[readIdx ^ 1];
    
    dst.cloneColumn(col, src);
}

// ============================================================================
// ReadSnapshot Implementation
// ============================================================================

bool ReadSnapshot::isValid(const World& world) const noexcept {
    return generation_ == world.generation();
}

} // namespace jupiter::ecs

