/**
 * @file chunk_mesh_pool.cpp
 * @brief Implementation of pre-allocated VulkanMesh pool for voxel chunks
 */

#include "rendering/chunk_mesh_pool.h"
#include "vulkan_backend.h"
#include "logging/logging.h"
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace rendering {

ChunkMeshPool::~ChunkMeshPool() {
    shutdown();
}

bool ChunkMeshPool::initialize(VkDevice device, VmaAllocator allocator, uint32_t poolSize) {
    if (initialized_) {
        LOG_WARN("ChunkMeshPool", "Already initialized");
        return false;
    }

    if (poolSize == 0 || poolSize > MAX_POOL_SIZE) {
        LOG_ERROR("ChunkMeshPool", "Invalid pool size: %u (max: %u)", poolSize, MAX_POOL_SIZE);
        return false;
    }

    device_ = device;
    allocator_ = allocator;
    poolSize_ = poolSize;

    LOG_INFO("ChunkMeshPool", "Initializing with %u mesh slots", poolSize);

    // Allocate slot storage using unique_ptr array
    slots_.reserve(poolSize);
    for (uint32_t i = 0; i < poolSize; ++i) {
        slots_.push_back(std::make_unique<MeshSlot>());
    }

    // Initialize free list
    freeList_.resize(poolSize);
    for (uint32_t i = 0; i < poolSize; ++i) {
        freeList_[i] = i;
    }
    freeListSize_.store(poolSize, std::memory_order_release);
    freeListHead_.store(0, std::memory_order_release);

    // Pre-allocate GPU buffers for each slot
    for (uint32_t i = 0; i < poolSize; ++i) {
        if (!createSlotBuffers(*slots_[i])) {
            LOG_ERROR("ChunkMeshPool", "Failed to create buffers for slot %u", i);
            shutdown();
            return false;
        }
    }

    initialized_ = true;

    // Calculate total memory usage
    size_t vertexBufferSize = MAX_VERTICES_PER_CHUNK * sizeof(VoxelVertexGPU);
    size_t indexBufferSize = MAX_INDICES_PER_CHUNK * sizeof(uint32_t);
    size_t totalPerSlot = vertexBufferSize + indexBufferSize;
    size_t totalMemory = totalPerSlot * poolSize;

    LOG_INFO("ChunkMeshPool", "Initialized successfully. Memory: %.2f MB (%zu bytes per slot)",
             totalMemory / (1024.0 * 1024.0), totalPerSlot);

    return true;
}

void ChunkMeshPool::shutdown() {
    if (!initialized_) return;

    LOG_INFO("ChunkMeshPool", "Shutting down");

    // Destroy all mesh slots
    for (auto& slot : slots_) {
        slot->mesh.destroy();
    }

    slots_.clear();
    freeList_.clear();
    freeListSize_.store(0, std::memory_order_release);
    freeListHead_.store(0, std::memory_order_release);

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    poolSize_ = 0;
    initialized_ = false;
}

ChunkMeshHandle ChunkMeshPool::acquire() {
    if (!initialized_) {
        return ChunkMeshHandle{};
    }

    // Simple atomic acquire from free list
    uint32_t head = freeListHead_.load(std::memory_order_acquire);
    uint32_t size = freeListSize_.load(std::memory_order_acquire);

    if (size == 0 || head >= poolSize_) {
        LOG_WARN("ChunkMeshPool", "Pool exhausted");
        return ChunkMeshHandle{};
    }

    // Try to atomically decrement size and get an index
    uint32_t expectedSize = size;
    while (!freeListSize_.compare_exchange_weak(expectedSize, expectedSize - 1,
                                                  std::memory_order_acq_rel)) {
        if (expectedSize == 0) {
            return ChunkMeshHandle{};
        }
    }

    // Get the index from free list
    uint32_t newHead = freeListHead_.fetch_add(1, std::memory_order_acq_rel);
    if (newHead >= poolSize_) {
        // Restore size on failure
        freeListSize_.fetch_add(1, std::memory_order_release);
        return ChunkMeshHandle{};
    }

    uint32_t slotIndex = freeList_[newHead % poolSize_];

    // Mark slot as in use
    MeshSlot& slot = *slots_[slotIndex];
    slot.inUse.store(true, std::memory_order_release);
    uint32_t version = slot.version.fetch_add(1, std::memory_order_acq_rel) + 1;

    return ChunkMeshHandle{slotIndex, version};
}

void ChunkMeshPool::release(ChunkMeshHandle handle) {
    if (!initialized_ || !isValid(handle)) {
        return;
    }

    MeshSlot& slot = *slots_[handle.index];

    // Mark slot as not in use
    slot.inUse.store(false, std::memory_order_release);

    // Reset vertex/index counts
    slot.currentVertexCount = 0;
    slot.currentIndexCount = 0;

    // Add back to free list (simple circular buffer approach)
    uint32_t size = freeListSize_.fetch_add(1, std::memory_order_acq_rel);
    uint32_t head = freeListHead_.load(std::memory_order_acquire);
    freeList_[(head + size) % poolSize_] = handle.index;
}

bool ChunkMeshPool::updateMesh(ChunkMeshHandle handle,
                                const VoxelVertexGPU* vertices, uint32_t vertexCount,
                                const uint32_t* indices, uint32_t indexCount) {
    if (!initialized_ || !isValid(handle)) {
        return false;
    }

    if (vertexCount > MAX_VERTICES_PER_CHUNK || indexCount > MAX_INDICES_PER_CHUNK) {
        LOG_ERROR("ChunkMeshPool", "Mesh data exceeds maximum: vertices %u/%u, indices %u/%u",
                  vertexCount, MAX_VERTICES_PER_CHUNK, indexCount, MAX_INDICES_PER_CHUNK);
        return false;
    }

    MeshSlot& slot = *slots_[handle.index];

    // Re-create mesh with new data
    // Since VulkanMesh doesn't support in-place updates, we destroy and recreate
    slot.mesh.destroy();

    std::vector<uint32_t> indexVector(indices, indices + indexCount);
    if (!slot.mesh.create(device_, allocator_,
                          vertices, vertexCount, sizeof(VoxelVertexGPU),
                          indexVector)) {
        LOG_ERROR("ChunkMeshPool", "Failed to update mesh for slot %u", handle.index);
        return false;
    }

    slot.usesCompactFormat = true;
    slot.currentVertexCount = vertexCount;
    slot.currentIndexCount = indexCount;

    return true;
}

bool ChunkMeshPool::updateMeshLit(ChunkMeshHandle handle,
                                   const Vertex3DLit* vertices, uint32_t vertexCount,
                                   const uint32_t* indices, uint32_t indexCount) {
    if (!initialized_ || !isValid(handle)) {
        return false;
    }

    // Calculate max vertices for Vertex3DLit (larger format)
    constexpr uint32_t MAX_LIT_VERTICES = MAX_VERTICES_PER_CHUNK;
    constexpr uint32_t MAX_LIT_INDICES = MAX_INDICES_PER_CHUNK;

    if (vertexCount > MAX_LIT_VERTICES || indexCount > MAX_LIT_INDICES) {
        LOG_ERROR("ChunkMeshPool", "Lit mesh data exceeds maximum: vertices %u/%u, indices %u/%u",
                  vertexCount, MAX_LIT_VERTICES, indexCount, MAX_LIT_INDICES);
        return false;
    }

    MeshSlot& slot = *slots_[handle.index];

    // Re-create mesh with new data
    slot.mesh.destroy();

    std::vector<Vertex3DLit> vertexVector(vertices, vertices + vertexCount);
    std::vector<uint32_t> indexVector(indices, indices + indexCount);
    if (!slot.mesh.create(device_, allocator_, vertexVector, indexVector)) {
        LOG_ERROR("ChunkMeshPool", "Failed to update lit mesh for slot %u", handle.index);
        return false;
    }

    slot.usesCompactFormat = false;
    slot.currentVertexCount = vertexCount;
    slot.currentIndexCount = indexCount;

    return true;
}

VulkanMesh* ChunkMeshPool::getMesh(ChunkMeshHandle handle) {
    if (!initialized_ || !isValid(handle)) {
        return nullptr;
    }
    return &slots_[handle.index]->mesh;
}

const VulkanMesh* ChunkMeshPool::getMesh(ChunkMeshHandle handle) const {
    if (!initialized_ || !isValid(handle)) {
        return nullptr;
    }
    return &slots_[handle.index]->mesh;
}

bool ChunkMeshPool::isValid(ChunkMeshHandle handle) const {
    if (!initialized_ || handle.index >= poolSize_) {
        return false;
    }

    const MeshSlot& slot = *slots_[handle.index];
    return slot.inUse.load(std::memory_order_acquire) &&
           slot.version.load(std::memory_order_acquire) == handle.version;
}

uint32_t ChunkMeshPool::getAvailableCount() const {
    return freeListSize_.load(std::memory_order_acquire);
}

bool ChunkMeshPool::createSlotBuffers(MeshSlot& slot) {
    // For now, we don't pre-allocate the actual VulkanMesh buffers.
    // VulkanMesh will allocate buffers when updateMesh is called.
    // This is simpler and still avoids runtime allocation during gameplay
    // (assuming chunks are loaded during loading screens or streamed).
    //
    // Future optimization: Pre-allocate fixed-size buffers and do
    // in-place updates via vkCmdUpdateBuffer or mapped memory.

    slot.usesCompactFormat = true;
    slot.currentVertexCount = 0;
    slot.currentIndexCount = 0;
    slot.version.store(0, std::memory_order_release);
    slot.inUse.store(false, std::memory_order_release);

    return true;
}

} // namespace rendering
} // namespace jupiter
