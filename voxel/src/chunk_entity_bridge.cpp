/**
 * @file chunk_entity_bridge.cpp
 * @brief Implementation of chunk-to-entity bridge
 */

#include "voxel/chunk_entity_bridge.h"
#include "voxel/voxel_mesher.h"
#include "ecs/entity_buffer.h"
#include "logging/logging.h"
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace voxel {

ChunkEntityBridge::~ChunkEntityBridge() {
    shutdown();
}

bool ChunkEntityBridge::initialize(const ChunkEntityBridgeConfig& config,
                                   VkDevice device,
                                   VmaAllocator allocator,
                                   ecs::EntityBuffer* entityBuffer) {
    if (initialized_) {
        LOG_WARN("ChunkEntityBridge", "Already initialized");
        return false;
    }

    if (!device || !allocator || !entityBuffer) {
        LOG_ERROR("ChunkEntityBridge", "Invalid parameters");
        return false;
    }

    config_ = config;
    device_ = device;
    allocator_ = allocator;
    entityBuffer_ = entityBuffer;

    // Initialize GPU mesh pool
    if (!meshPool_.initialize(device, allocator, config.maxChunkEntities)) {
        LOG_ERROR("ChunkEntityBridge", "Failed to initialize mesh pool");
        return false;
    }

    // Initialize physics mesh pool if enabled
    if (config.enablePhysicsMesh) {
        if (!physicsMeshPool_.initialize(config.physicsPoolSize)) {
            LOG_WARN("ChunkEntityBridge", "Failed to initialize physics mesh pool");
            // Not fatal - continue without physics
        }
    }

    // Pre-allocate conversion buffers
    // Max vertices for Vertex3DLit format
    litVertexBuffer_.reserve(rendering::ChunkMeshPool::MAX_VERTICES_PER_CHUNK);
    indexBuffer_.reserve(rendering::ChunkMeshPool::MAX_INDICES_PER_CHUNK);

    // Reserve space in maps
    chunkStates_.reserve(config.maxChunkEntities);
    entityToCoord_.reserve(config.maxChunkEntities);

    initialized_ = true;
    LOG_INFO("ChunkEntityBridge", "Initialized with %u max entities, LOD distance %u",
             config.maxChunkEntities, config.lodTransitionDistance);

    return true;
}

void ChunkEntityBridge::shutdown() {
    if (!initialized_) return;

    LOG_INFO("ChunkEntityBridge", "Shutting down with %zu active chunks",
             chunkStates_.size());

    // Destroy all entities
    for (auto& [key, state] : chunkStates_) {
        if (state.meshHandle.isValid()) {
            meshPool_.release(state.meshHandle);
        }
    }

    chunkStates_.clear();
    entityToCoord_.clear();

    meshPool_.shutdown();
    physicsMeshPool_.shutdown();

    litVertexBuffer_.clear();
    indexBuffer_.clear();

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    entityBuffer_ = nullptr;
    initialized_ = false;
}

void ChunkEntityBridge::onChunkMeshed(const ChunkCoord& coord,
                                       uint32_t chunkPoolIndex,
                                       const void* stbVertices,
                                       uint32_t vertexCount,
                                       const glm::vec3& scale,
                                       const glm::vec3& aabbMin,
                                       const glm::vec3& aabbMax) {
    if (!initialized_) return;

    uint64_t key = coordToKey(coord);
    auto it = chunkStates_.find(key);

    ChunkEntityState* state = nullptr;

    if (it == chunkStates_.end()) {
        // Create new entity
        ecs::EntityId entityId = createChunkEntity(coord, chunkPoolIndex);
        if (entityId == ecs::INVALID_ENTITY) {
            LOG_WARN("ChunkEntityBridge", "Failed to create entity for chunk (%d,%d,%d)",
                     coord.x, coord.y, coord.z);
            return;
        }

        auto [insertIt, inserted] = chunkStates_.emplace(key, ChunkEntityState{});
        state = &insertIt->second;
        state->entityId = entityId;
        state->chunkPoolIndex = chunkPoolIndex;
        entityToCoord_[entityId] = key;

        // Acquire mesh slot
        state->meshHandle = meshPool_.acquire();
        if (!state->meshHandle.isValid()) {
            LOG_WARN("ChunkEntityBridge", "Mesh pool exhausted for chunk (%d,%d,%d)",
                     coord.x, coord.y, coord.z);
            // Entity exists but no GPU mesh - will be culled
        }

        // Notify callback
        if (entityCreatedCallback_) {
            // Find entity index in buffer (simplistic - assumes entityId is index)
            uint32_t entityIndex = entityId; // In real impl, need proper lookup
            entityCreatedCallback_(coord, entityId, entityIndex);
        }
    } else {
        state = &it->second;
    }

    if (!state || !state->meshHandle.isValid()) return;

    // Determine LOD (we don't have camera pos here, use last known or default)
    uint8_t targetLod = state->currentLod;
    if (targetLod == 255) {
        // First mesh, default to distant LOD (compact format)
        targetLod = 2;
    }

    // Upload mesh
    if (!uploadMesh(*state, stbVertices, vertexCount, scale, targetLod)) {
        LOG_WARN("ChunkEntityBridge", "Failed to upload mesh for chunk (%d,%d,%d)",
                 coord.x, coord.y, coord.z);
        return;
    }

    state->meshGeneration++;

    // Notify mesh updated callback
    if (meshUpdatedCallback_) {
        rendering::VulkanMesh* mesh = meshPool_.getMesh(state->meshHandle);
        glm::vec3 worldPos = coord.toWorldPos();
        bool usesCompact = state->currentLod >= 2;
        meshUpdatedCallback_(coord, state->entityId, mesh, worldPos, scale,
                            state->currentLod, usesCompact);
    }
}

void ChunkEntityBridge::onChunkUnloaded(const ChunkCoord& coord,
                                         uint32_t chunkPoolIndex) {
    if (!initialized_) return;

    uint64_t key = coordToKey(coord);
    auto it = chunkStates_.find(key);

    if (it == chunkStates_.end()) {
        return;
    }

    ChunkEntityState& state = it->second;

    // Notify callback before destroying
    if (entityDestroyedCallback_) {
        entityDestroyedCallback_(coord, state.entityId);
    }

    // Release GPU mesh
    if (state.meshHandle.isValid()) {
        meshPool_.release(state.meshHandle);
    }

    // Remove from maps
    entityToCoord_.erase(state.entityId);
    chunkStates_.erase(it);
}

void ChunkEntityBridge::updateLods(const glm::vec3& cameraPos) {
    if (!initialized_) return;

    for (auto& [key, state] : chunkStates_) {
        if (!state.meshHandle.isValid()) continue;

        // Reconstruct coord from state (or we could store it)
        // For now, we calculate LOD based on stored chunk pool index
        // This is a simplification - in production, store coord in state
        uint8_t newLod = state.currentLod; // Keep current as fallback

        // If we need LOD transition, we'd re-upload the mesh here
        // This requires storing the raw vertex data or re-requesting from VoxelWorld
        // For now, just track the LOD level

        if (newLod != state.currentLod) {
            state.currentLod = newLod;
            // Would trigger mesh re-upload here
        }
    }
}

ecs::EntityId ChunkEntityBridge::getEntityId(const ChunkCoord& coord) const {
    uint64_t key = coordToKey(coord);
    auto it = chunkStates_.find(key);
    if (it == chunkStates_.end()) {
        return ecs::INVALID_ENTITY;
    }
    return it->second.entityId;
}

const ChunkEntityState* ChunkEntityBridge::getChunkState(const ChunkCoord& coord) const {
    uint64_t key = coordToKey(coord);
    auto it = chunkStates_.find(key);
    if (it == chunkStates_.end()) {
        return nullptr;
    }
    return &it->second;
}

rendering::ChunkMeshHandle ChunkEntityBridge::getMeshHandle(const ChunkCoord& coord) const {
    uint64_t key = coordToKey(coord);
    auto it = chunkStates_.find(key);
    if (it == chunkStates_.end()) {
        return rendering::ChunkMeshHandle{};
    }
    return it->second.meshHandle;
}

rendering::VulkanMesh* ChunkEntityBridge::getMesh(const ChunkCoord& coord) {
    auto handle = getMeshHandle(coord);
    if (!handle.isValid()) {
        return nullptr;
    }
    return meshPool_.getMesh(handle);
}

uint32_t ChunkEntityBridge::getAvailableMeshSlots() const {
    return meshPool_.getAvailableCount();
}

uint32_t ChunkEntityBridge::getAvailablePhysicsBuffers() const {
    return physicsMeshPool_.getAvailableCount();
}

ecs::EntityId ChunkEntityBridge::createChunkEntity(const ChunkCoord& coord,
                                                    uint32_t chunkPoolIndex) {
    if (!entityBuffer_) return ecs::INVALID_ENTITY;

    // Generate entity ID
    ecs::EntityId entityId = nextEntityId_++;

    // In a full implementation, we'd add the entity to the EntityBuffer
    // and set up all the columns. For now, we just track the ID.

    // The entity would have:
    // - Position = chunk world position
    // - VoxelChunk flag
    // - ChunkPoolIndex column
    // - VoxelLodLevel column
    // - NetworkDirtyBits column

    return entityId;
}

void ChunkEntityBridge::destroyChunkEntity(const ChunkCoord& coord) {
    // In a full implementation, remove from EntityBuffer
    // For now, handled in onChunkUnloaded
}

uint8_t ChunkEntityBridge::calculateLod(const ChunkCoord& coord,
                                         const glm::vec3& cameraPos) const {
    // Calculate distance in chunks
    glm::vec3 chunkCenter = coord.toWorldPos() + glm::vec3(CHUNK_SIZE * 0.5f);
    float distance = glm::length(cameraPos - chunkCenter);
    float distanceInChunks = distance / CHUNK_SIZE;

    // LOD levels:
    // 0-1: High detail (Vertex3DLit, 48 bytes)
    // 2+: Distant (VoxelVertexGPU, 8 bytes)
    if (distanceInChunks < static_cast<float>(config_.lodTransitionDistance)) {
        return distanceInChunks < 2.0f ? 0 : 1;
    }
    return 2;
}

bool ChunkEntityBridge::uploadMesh(ChunkEntityState& state,
                                    const void* stbVertices,
                                    uint32_t vertexCount,
                                    const glm::vec3& scale,
                                    uint8_t targetLod) {
    if (!state.meshHandle.isValid() || vertexCount == 0) {
        return false;
    }

    // stb_voxel_render outputs quads (4 vertices each)
    if (vertexCount % 4 != 0) {
        LOG_ERROR("ChunkEntityBridge", "Invalid vertex count %u (not multiple of 4)",
                  vertexCount);
        return false;
    }

    uint32_t numQuads = vertexCount / 4;
    uint32_t indexCount = numQuads * 6; // 2 triangles per quad

    // Generate indices for quads
    indexBuffer_.resize(indexCount);
    for (uint32_t q = 0; q < numQuads; ++q) {
        uint32_t base = q * 4;
        indexBuffer_[q * 6 + 0] = base + 0;
        indexBuffer_[q * 6 + 1] = base + 1;
        indexBuffer_[q * 6 + 2] = base + 2;
        indexBuffer_[q * 6 + 3] = base + 0;
        indexBuffer_[q * 6 + 4] = base + 2;
        indexBuffer_[q * 6 + 5] = base + 3;
    }

    bool success = false;

    if (targetLod < 2) {
        // High LOD: convert to Vertex3DLit (48 bytes)
        litVertexBuffer_.resize(vertexCount);
        const StbVoxelVertex* stbVerts = static_cast<const StbVoxelVertex*>(stbVertices);
        VoxelMeshConverter::convertToLit(
            stbVerts, vertexCount,
            litVertexBuffer_.data(),
            glm::vec3(0.0f), // chunk offset applied elsewhere
            scale);

        success = meshPool_.updateMeshLit(
            state.meshHandle,
            litVertexBuffer_.data(), vertexCount,
            indexBuffer_.data(), indexCount);
    } else {
        // Low LOD: use compact 8-byte format directly
        const rendering::VoxelVertexGPU* gpuVerts =
            static_cast<const rendering::VoxelVertexGPU*>(stbVertices);

        success = meshPool_.updateMesh(
            state.meshHandle,
            gpuVerts, vertexCount,
            indexBuffer_.data(), indexCount);
    }

    if (success) {
        state.currentLod = targetLod;
    }

    return success;
}

} // namespace voxel
} // namespace jupiter
