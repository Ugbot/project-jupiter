#pragma once

/**
 * @file chunk_mesh_pool.h
 * @brief Pre-allocated pool of VulkanMesh objects for voxel chunks
 *
 * Manages GPU mesh resources for voxel rendering.
 * Following Project Jupiter principles:
 * - All VulkanMesh objects pre-allocated at initialization
 * - Lock-free slot acquisition/release
 * - No runtime GPU allocations during gameplay
 * - Supports mesh data updates for chunk remeshing
 */

#include "vulkan_mesh.h"
#include "vertex_formats.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace jupiter {
namespace rendering {

/**
 * @brief Handle to a chunk mesh in the pool
 *
 * Type-safe index with version for validation.
 */
struct ChunkMeshHandle {
    uint32_t index = UINT32_MAX;
    uint32_t version = 0;

    bool isValid() const { return index != UINT32_MAX; }

    bool operator==(const ChunkMeshHandle& other) const {
        return index == other.index && version == other.version;
    }

    bool operator!=(const ChunkMeshHandle& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Pre-allocated pool of VulkanMesh for voxel chunks
 *
 * Design:
 * - Fixed number of mesh slots allocated at initialization
 * - Each slot contains vertex and index buffers sized for max chunk geometry
 * - Lock-free acquire/release using atomic free list
 * - Supports in-place mesh data updates (no reallocation)
 *
 * Memory layout:
 * - Each mesh slot has:
 *   - Vertex buffer: MAX_VERTICES * sizeof(VoxelVertexGPU) = 32KB * 8 = 256KB
 *   - Index buffer: MAX_INDICES * sizeof(uint32_t) = 48KB * 4 = 192KB
 *   - Total per slot: ~448KB
 *
 * Thread safety:
 * - acquire() and release() are lock-free
 * - updateMesh() must not be called while mesh is being rendered
 *   (use per-frame double buffering at higher level if needed)
 */
class ChunkMeshPool {
public:
    /// Maximum quads per chunk mesh (same as MeshBuffer)
    static constexpr uint32_t MAX_QUADS_PER_CHUNK = 8192;

    /// Maximum vertices per chunk (4 per quad)
    static constexpr uint32_t MAX_VERTICES_PER_CHUNK = MAX_QUADS_PER_CHUNK * 4;

    /// Maximum indices per chunk (6 per quad)
    static constexpr uint32_t MAX_INDICES_PER_CHUNK = MAX_QUADS_PER_CHUNK * 6;

    /// Default pool size (supports ~64 visible chunks)
    static constexpr uint32_t DEFAULT_POOL_SIZE = 64;

    /// Maximum pool size
    static constexpr uint32_t MAX_POOL_SIZE = 256;

    ChunkMeshPool() = default;
    ~ChunkMeshPool();

    // Non-copyable, non-movable (owns GPU resources)
    ChunkMeshPool(const ChunkMeshPool&) = delete;
    ChunkMeshPool& operator=(const ChunkMeshPool&) = delete;
    ChunkMeshPool(ChunkMeshPool&&) = delete;
    ChunkMeshPool& operator=(ChunkMeshPool&&) = delete;

    /**
     * @brief Initialize the pool with pre-allocated GPU meshes
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param poolSize Number of mesh slots to allocate
     * @return true if initialization successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator,
                   uint32_t poolSize = DEFAULT_POOL_SIZE);

    /**
     * @brief Shutdown and free all GPU resources
     */
    void shutdown();

    /**
     * @brief Acquire a mesh slot from the pool (lock-free)
     *
     * @return Handle to mesh slot, or invalid handle if pool exhausted
     */
    ChunkMeshHandle acquire();

    /**
     * @brief Release a mesh slot back to the pool (lock-free)
     *
     * @param handle Handle to release
     */
    void release(ChunkMeshHandle handle);

    /**
     * @brief Update mesh data for a slot (VoxelVertexGPU format)
     *
     * Uploads new vertex/index data to the existing GPU buffers.
     * The mesh must have been acquired and not released.
     *
     * @param handle Handle to mesh slot
     * @param vertices Pointer to VoxelVertexGPU vertex data
     * @param vertexCount Number of vertices (must be <= MAX_VERTICES_PER_CHUNK)
     * @param indices Pointer to index data
     * @param indexCount Number of indices (must be <= MAX_INDICES_PER_CHUNK)
     * @return true if update successful
     */
    bool updateMesh(ChunkMeshHandle handle,
                   const VoxelVertexGPU* vertices, uint32_t vertexCount,
                   const uint32_t* indices, uint32_t indexCount);

    /**
     * @brief Update mesh data for a slot (Vertex3DLit format for nearby chunks)
     *
     * @param handle Handle to mesh slot
     * @param vertices Pointer to Vertex3DLit vertex data
     * @param vertexCount Number of vertices
     * @param indices Pointer to index data
     * @param indexCount Number of indices
     * @return true if update successful
     */
    bool updateMeshLit(ChunkMeshHandle handle,
                       const Vertex3DLit* vertices, uint32_t vertexCount,
                       const uint32_t* indices, uint32_t indexCount);

    /**
     * @brief Get VulkanMesh pointer for rendering
     *
     * @param handle Handle to mesh slot
     * @return Pointer to VulkanMesh, or nullptr if handle invalid
     */
    VulkanMesh* getMesh(ChunkMeshHandle handle);
    const VulkanMesh* getMesh(ChunkMeshHandle handle) const;

    /**
     * @brief Check if a handle is valid
     */
    bool isValid(ChunkMeshHandle handle) const;

    /**
     * @brief Get number of available slots
     */
    uint32_t getAvailableCount() const;

    /**
     * @brief Get total number of slots
     */
    uint32_t getTotalCount() const { return poolSize_; }

    /**
     * @brief Get number of slots in use
     */
    uint32_t getUsedCount() const { return poolSize_ - getAvailableCount(); }

    /**
     * @brief Check if pool is initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    /**
     * @brief Per-slot metadata
     */
    struct MeshSlot {
        VulkanMesh mesh;
        std::atomic<uint32_t> version{0};
        std::atomic<bool> inUse{false};

        // Track current vertex format (for proper binding)
        bool usesCompactFormat = true;
        uint32_t currentVertexCount = 0;
        uint32_t currentIndexCount = 0;
    };

    // Pre-allocated mesh slots (unique_ptr because MeshSlot contains non-copyable VulkanMesh)
    std::vector<std::unique_ptr<MeshSlot>> slots_;

    // Lock-free free list (simple atomic counter for now)
    std::atomic<uint32_t> freeListHead_{0};
    std::vector<uint32_t> freeList_;
    std::atomic<uint32_t> freeListSize_{0};

    // Vulkan resources
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Pool configuration
    uint32_t poolSize_ = 0;
    bool initialized_ = false;

    // Helper: Create pre-sized GPU buffers for a slot
    bool createSlotBuffers(MeshSlot& slot);
};

} // namespace rendering
} // namespace jupiter
