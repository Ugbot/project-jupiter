#pragma once

#include "voxel_types.h"
#include "chunk_pool.h"
#include "chunk_map.h"
#include "dirty_queue.h"
#include "mesh_buffer_pool.h"
#include "streaming_manager.h"
#include "voxel_mesher.h"

#include <glm/glm.hpp>
#include <functional>
#include <memory>

/**
 * @file voxel_world.h
 * @brief Main voxel world interface
 *
 * Coordinates chunk loading, meshing, and editing.
 * Designed to be renderer-agnostic (headless compatible).
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Configuration for VoxelWorld
 */
struct VoxelWorldConfig {
    int viewDistance = DEFAULT_VIEW_DISTANCE;  ///< View distance in chunks
    uint32_t maxChunks = MAX_ACTIVE_CHUNKS;    ///< Maximum loaded chunks
    uint32_t seed = 0;                         ///< World generation seed
    float targetFrameMs = 16.67f;              ///< Target frame time for budgeting
    float meshingBudgetPercent = 0.15f;        ///< Frame % for meshing (15%)
};

/**
 * @brief Callback for when a chunk mesh is ready
 */
using ChunkMeshCallback = std::function<void(
    const ChunkCoord& coord,
    uint32_t poolIndex,
    const VoxelVertex* vertices,
    uint32_t vertexCount,
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax
)>;

/**
 * @brief Callback for when a chunk is unloaded
 */
using ChunkUnloadCallback = std::function<void(
    const ChunkCoord& coord,
    uint32_t poolIndex
)>;

/**
 * @brief Callback for raw stb_voxel_render mesh data
 *
 * Used by ChunkEntityBridge to directly receive 8-byte vertices
 * for GPU upload without intermediate conversion.
 */
using RawMeshCallback = std::function<void(
    const ChunkCoord& coord,
    uint32_t poolIndex,
    const void* stbVertices,       ///< Raw stb_voxel_render vertices (8 bytes each)
    uint32_t vertexCount,
    const glm::vec3& scale,        ///< Scale factor for position unpacking
    const glm::vec3& aabbMin,
    const glm::vec3& aabbMax
)>;

/**
 * @brief Raycast result for voxel intersection
 */
struct VoxelRaycastResult {
    bool hit = false;              ///< True if hit a solid block
    glm::ivec3 blockPos;           ///< World position of hit block
    glm::ivec3 blockNormal;        ///< Face normal of hit
    float distance = 0.0f;         ///< Distance to hit
    BlockType blockType = BLOCK_AIR;  ///< Type of hit block
    ChunkCoord chunkCoord;         ///< Chunk containing hit block
};

/**
 * @brief Main voxel world manager
 *
 * Coordinates:
 * - Chunk memory (ChunkPool)
 * - Chunk lookup (ChunkMap)
 * - Streaming (StreamingManager)
 * - Dirty tracking (DirtyChunkQueue)
 * - Meshing (VoxelMesher)
 *
 * This class is renderer-agnostic. Use callbacks to receive
 * mesh data for GPU upload.
 */
class VoxelWorld {
public:
    VoxelWorld() = default;
    ~VoxelWorld();

    // Non-copyable, non-movable
    VoxelWorld(const VoxelWorld&) = delete;
    VoxelWorld& operator=(const VoxelWorld&) = delete;
    VoxelWorld(VoxelWorld&&) = delete;
    VoxelWorld& operator=(VoxelWorld&&) = delete;

    /**
     * @brief Initialize the voxel world
     *
     * @param config Configuration
     * @return true if successful
     */
    bool initialize(const VoxelWorldConfig& config = {});

    /**
     * @brief Shutdown the voxel world
     */
    void shutdown();

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Set callback for chunk mesh completion
     */
    void setMeshCallback(ChunkMeshCallback callback) {
        meshCallback_ = std::move(callback);
    }

    /**
     * @brief Set callback for chunk unload
     */
    void setUnloadCallback(ChunkUnloadCallback callback) {
        unloadCallback_ = std::move(callback);
    }

    /**
     * @brief Set callback for raw stb mesh data
     *
     * Used by ChunkEntityBridge for direct GPU upload.
     * Called in addition to meshCallback_ when both are set.
     */
    void setRawMeshCallback(RawMeshCallback callback) {
        rawMeshCallback_ = std::move(callback);
    }

    // ========================================================================
    // Per-Frame Update
    // ========================================================================

    /**
     * @brief Update the voxel world
     *
     * Call once per frame. Handles:
     * - Streaming (load/unload chunks based on camera)
     * - Meshing dirty chunks
     * - Processing edit queue
     *
     * @param cameraPos Camera world position
     * @param deltaTime Frame delta time in seconds
     */
    void update(const glm::vec3& cameraPos, float deltaTime);

    // ========================================================================
    // Block Access
    // ========================================================================

    /**
     * @brief Get block at world position
     *
     * @param worldPos World position
     * @return Block type, or BLOCK_AIR if chunk not loaded
     */
    BlockType getBlock(const glm::ivec3& worldPos) const;

    /**
     * @brief Set block at world position
     *
     * Queues the edit for processing. Chunk will be re-meshed.
     *
     * @param worldPos World position
     * @param type Block type to set
     * @return true if edit was queued (chunk is loaded)
     */
    bool setBlock(const glm::ivec3& worldPos, BlockType type);

    /**
     * @brief Check if block is solid (non-air)
     *
     * @param worldPos World position
     * @return true if solid
     */
    bool isSolid(const glm::ivec3& worldPos) const;

    // ========================================================================
    // Raycast
    // ========================================================================

    /**
     * @brief Cast ray against voxel world
     *
     * Uses DDA algorithm for efficient traversal.
     *
     * @param origin Ray origin
     * @param direction Ray direction (normalized)
     * @param maxDistance Maximum ray distance
     * @return Raycast result
     */
    VoxelRaycastResult raycast(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float maxDistance) const;

    // ========================================================================
    // Chunk Access
    // ========================================================================

    /**
     * @brief Check if chunk is loaded
     *
     * @param coord Chunk coordinate
     * @return true if loaded
     */
    bool isChunkLoaded(const ChunkCoord& coord) const;

    /**
     * @brief Get chunk pool index
     *
     * @param coord Chunk coordinate
     * @return Pool index, or INVALID_INDEX if not loaded
     */
    uint32_t getChunkIndex(const ChunkCoord& coord) const;

    /**
     * @brief Get chunk voxel data (const)
     *
     * @param coord Chunk coordinate
     * @return Pointer to data, or nullptr if not loaded
     */
    const ChunkVoxelData* getChunkData(const ChunkCoord& coord) const;

    /**
     * @brief Force load a chunk (synchronous)
     *
     * @param coord Chunk coordinate
     * @return Pool index, or INVALID_INDEX if pool exhausted
     */
    uint32_t loadChunk(const ChunkCoord& coord);

    /**
     * @brief Force unload a chunk
     *
     * @param coord Chunk coordinate
     */
    void unloadChunk(const ChunkCoord& coord);

    // ========================================================================
    // Statistics
    // ========================================================================

    /**
     * @brief Get number of loaded chunks
     */
    uint32_t getLoadedChunkCount() const;

    /**
     * @brief Get number of pending mesh operations
     */
    uint32_t getPendingMeshCount() const;

    /**
     * @brief Get chunks meshed this frame
     */
    uint32_t getChunksMeshedThisFrame() const;

    /**
     * @brief Get configuration
     */
    const VoxelWorldConfig& getConfig() const { return config_; }

private:
    // Generate terrain for a newly loaded chunk
    void generateChunkTerrain(ChunkVoxelData* chunk, const ChunkCoord& coord);

    // Mesh a single chunk
    void meshChunk(const ChunkCoord& coord, uint32_t poolIndex);

    // Get neighbor chunk data for meshing
    void getNeighborChunks(const ChunkCoord& coord,
                          const ChunkVoxelData* neighbors[6]) const;

    VoxelWorldConfig config_;
    bool initialized_ = false;

    // Subsystems (heap-allocated via pointers to avoid large stack objects)
    std::unique_ptr<ChunkPool> chunkPool_;
    std::unique_ptr<ChunkMap> chunkMap_;
    std::unique_ptr<DirtyChunkQueue> dirtyQueue_;
    std::unique_ptr<StreamingManager> streamingManager_;
    std::unique_ptr<MeshingBudgeter> meshingBudgeter_;
    std::unique_ptr<MeshBufferPool> meshBufferPool_;
    std::unique_ptr<VoxelMesher> mesher_;

    // Converted vertex buffer for mesh callback (pre-allocated)
    std::unique_ptr<VoxelVertex[]> convertedVertices_;
    static constexpr uint32_t MAX_CONVERTED_VERTICES = MeshBuffer::MAX_VERTICES;

    // Raw stb vertex buffer for raw mesh callback (pre-allocated)
    std::unique_ptr<StbVoxelVertex[]> rawStbVertices_;

    // Callbacks
    ChunkMeshCallback meshCallback_;
    ChunkUnloadCallback unloadCallback_;
    RawMeshCallback rawMeshCallback_;

    // Per-frame stats
    uint32_t chunksMeshedThisFrame_ = 0;
};

} // namespace voxel
} // namespace jupiter
