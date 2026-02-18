#pragma once

/**
 * @file chunk_entity_bridge.h
 * @brief Bridges voxel chunks to ECS entities for integrated game systems
 *
 * Handles:
 * - Creating ECS entities from voxel chunks
 * - GPU mesh pool allocation (ChunkMeshPool)
 * - LOD transitions and vertex format conversion
 * - Physics mesh generation
 * - Network dirty tracking
 *
 * Following Project Jupiter principles:
 * - No runtime allocation during gameplay
 * - Pre-allocated pools for all resources
 * - Lock-free where possible
 */

#include "voxel_types.h"
#include "voxel_mesh_converter.h"
#include "voxel_physics_mesh.h"
#include "rendering/chunk_mesh_pool.h"
#include "ecs/types.h"
#include <glm/glm.hpp>
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace jupiter {

// Forward declarations
namespace ecs {
    struct EntityBuffer;
}

namespace voxel {

// Forward declarations
class VoxelWorld;

/**
 * @brief Configuration for the chunk entity bridge
 */
struct ChunkEntityBridgeConfig {
    uint32_t maxChunkEntities = 256;      ///< Maximum chunk entities
    uint32_t lodTransitionDistance = 4;    ///< Chunks within this distance use Vertex3DLit
    bool enablePhysicsMesh = true;         ///< Generate physics collision meshes
    uint32_t physicsPoolSize = 32;         ///< Physics mesh buffer pool size
};

/**
 * @brief Tracks chunk entity state
 */
struct ChunkEntityState {
    ecs::EntityId entityId = ecs::INVALID_ENTITY;
    rendering::ChunkMeshHandle meshHandle;
    uint32_t chunkPoolIndex = UINT32_MAX;
    uint8_t currentLod = 255;              ///< 255 = uninitialized
    bool hasPhysicsMesh = false;
    uint32_t physicsTriangleCount = 0;
    uint64_t meshGeneration = 0;           ///< Tracks remeshing
};

/**
 * @brief Callback for when an entity is created for a chunk
 */
using EntityCreatedCallback = std::function<void(
    const ChunkCoord& coord,
    ecs::EntityId entityId,
    uint32_t entityIndex
)>;

/**
 * @brief Callback for when a chunk entity is destroyed
 */
using EntityDestroyedCallback = std::function<void(
    const ChunkCoord& coord,
    ecs::EntityId entityId
)>;

/**
 * @brief Callback for mesh update (provides data for SceneManager Renderable)
 */
using MeshUpdatedCallback = std::function<void(
    const ChunkCoord& coord,
    ecs::EntityId entityId,
    rendering::VulkanMesh* mesh,
    const glm::vec3& chunkWorldPos,
    const glm::vec3& scale,
    uint8_t lodLevel,
    bool usesCompactFormat
)>;

/**
 * @brief Bridges voxel chunks to ECS entities
 *
 * This class manages the lifecycle of chunk entities:
 * 1. VoxelWorld produces mesh data via callback
 * 2. ChunkEntityBridge creates/updates ECS entities
 * 3. GPU mesh resources managed via ChunkMeshPool
 * 4. LOD determines vertex format (8-byte vs 48-byte)
 * 5. Optional physics mesh generation
 *
 * Usage:
 * @code
 * ChunkEntityBridge bridge;
 * bridge.initialize(config, device, allocator, entityBuffer);
 *
 * // Set up VoxelWorld mesh callback
 * voxelWorld.setMeshCallback([&](auto& coord, auto poolIndex, auto* verts, ...) {
 *     bridge.onChunkMeshed(coord, poolIndex, rawStbData, vertexCount, ...);
 * });
 *
 * // Set callback for new entities
 * bridge.setEntityCreatedCallback([&](auto& coord, auto entityId, auto index) {
 *     // Add to SceneManager, etc.
 * });
 *
 * // Per frame: update LODs based on camera
 * bridge.updateLods(cameraPos);
 * @endcode
 */
class ChunkEntityBridge {
public:
    ChunkEntityBridge() = default;
    ~ChunkEntityBridge();

    // Non-copyable
    ChunkEntityBridge(const ChunkEntityBridge&) = delete;
    ChunkEntityBridge& operator=(const ChunkEntityBridge&) = delete;

    /**
     * @brief Initialize the bridge
     *
     * @param config Bridge configuration
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param entityBuffer ECS entity buffer for entity creation
     * @return true if successful
     */
    bool initialize(const ChunkEntityBridgeConfig& config,
                   VkDevice device,
                   VmaAllocator allocator,
                   ecs::EntityBuffer* entityBuffer);

    /**
     * @brief Shutdown and release all resources
     */
    void shutdown();

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }

    // ========================================================================
    // Callbacks
    // ========================================================================

    void setEntityCreatedCallback(EntityCreatedCallback cb) {
        entityCreatedCallback_ = std::move(cb);
    }

    void setEntityDestroyedCallback(EntityDestroyedCallback cb) {
        entityDestroyedCallback_ = std::move(cb);
    }

    void setMeshUpdatedCallback(MeshUpdatedCallback cb) {
        meshUpdatedCallback_ = std::move(cb);
    }

    // ========================================================================
    // Chunk Events (called from VoxelWorld callbacks)
    // ========================================================================

    /**
     * @brief Handle chunk mesh completion
     *
     * Called when VoxelWorld finishes meshing a chunk.
     * Creates entity if needed, uploads mesh to GPU.
     *
     * @param coord Chunk coordinate
     * @param chunkPoolIndex Index in VoxelWorld's ChunkPool
     * @param stbVertices Raw stb_voxel_render vertices (8 bytes each)
     * @param vertexCount Number of vertices
     * @param scale Scale factor from stb_voxel_render
     * @param aabbMin Mesh AABB min
     * @param aabbMax Mesh AABB max
     */
    void onChunkMeshed(const ChunkCoord& coord,
                       uint32_t chunkPoolIndex,
                       const void* stbVertices,
                       uint32_t vertexCount,
                       const glm::vec3& scale,
                       const glm::vec3& aabbMin,
                       const glm::vec3& aabbMax);

    /**
     * @brief Handle chunk unload
     *
     * Called when VoxelWorld unloads a chunk.
     * Destroys entity, releases GPU mesh.
     *
     * @param coord Chunk coordinate
     * @param chunkPoolIndex Index in VoxelWorld's ChunkPool
     */
    void onChunkUnloaded(const ChunkCoord& coord,
                         uint32_t chunkPoolIndex);

    // ========================================================================
    // LOD Management
    // ========================================================================

    /**
     * @brief Update LOD levels based on camera position
     *
     * Chunks within lodTransitionDistance use Vertex3DLit format (48 bytes).
     * Distant chunks use VoxelVertexGPU format (8 bytes).
     *
     * @param cameraPos Camera world position
     */
    void updateLods(const glm::vec3& cameraPos);

    /**
     * @brief Set LOD transition distance
     *
     * @param distance Distance in chunks
     */
    void setLodTransitionDistance(uint32_t distance) {
        config_.lodTransitionDistance = distance;
    }

    // ========================================================================
    // Entity Access
    // ========================================================================

    /**
     * @brief Get entity ID for a chunk
     *
     * @param coord Chunk coordinate
     * @return Entity ID, or INVALID_ENTITY if not found
     */
    ecs::EntityId getEntityId(const ChunkCoord& coord) const;

    /**
     * @brief Get chunk state for a coordinate
     *
     * @param coord Chunk coordinate
     * @return Pointer to state, or nullptr if not found
     */
    const ChunkEntityState* getChunkState(const ChunkCoord& coord) const;

    /**
     * @brief Get mesh handle for a chunk
     *
     * @param coord Chunk coordinate
     * @return Mesh handle, or invalid handle if not found
     */
    rendering::ChunkMeshHandle getMeshHandle(const ChunkCoord& coord) const;

    /**
     * @brief Get VulkanMesh for a chunk
     *
     * @param coord Chunk coordinate
     * @return Pointer to mesh, or nullptr if not found
     */
    rendering::VulkanMesh* getMesh(const ChunkCoord& coord);

    // ========================================================================
    // Statistics
    // ========================================================================

    uint32_t getChunkEntityCount() const { return static_cast<uint32_t>(chunkStates_.size()); }
    uint32_t getAvailableMeshSlots() const;
    uint32_t getAvailablePhysicsBuffers() const;

private:
    /**
     * @brief Create a new entity for a chunk
     */
    ecs::EntityId createChunkEntity(const ChunkCoord& coord, uint32_t chunkPoolIndex);

    /**
     * @brief Destroy a chunk entity
     */
    void destroyChunkEntity(const ChunkCoord& coord);

    /**
     * @brief Calculate LOD level for a chunk based on distance
     */
    uint8_t calculateLod(const ChunkCoord& coord, const glm::vec3& cameraPos) const;

    /**
     * @brief Upload mesh with appropriate format for LOD
     */
    bool uploadMesh(ChunkEntityState& state,
                   const void* stbVertices,
                   uint32_t vertexCount,
                   const glm::vec3& scale,
                   uint8_t targetLod);

    ChunkEntityBridgeConfig config_;
    bool initialized_ = false;

    // Vulkan resources
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // ECS integration
    ecs::EntityBuffer* entityBuffer_ = nullptr;
    ecs::EntityId nextEntityId_ = 1;

    // GPU mesh pool
    rendering::ChunkMeshPool meshPool_;

    // Vertex converter
    VoxelMeshConverter meshConverter_;

    // Physics mesh pool
    PhysicsMeshPool physicsMeshPool_;
    VoxelPhysicsMesh physicsGenerator_;

    // Chunk state map (coord -> state)
    std::unordered_map<uint64_t, ChunkEntityState> chunkStates_;

    // Entity -> coord reverse lookup
    std::unordered_map<ecs::EntityId, uint64_t> entityToCoord_;

    // Callbacks
    EntityCreatedCallback entityCreatedCallback_;
    EntityDestroyedCallback entityDestroyedCallback_;
    MeshUpdatedCallback meshUpdatedCallback_;

    // Temporary buffers for vertex conversion (pre-allocated)
    std::vector<rendering::Vertex3DLit> litVertexBuffer_;
    std::vector<uint32_t> indexBuffer_;

    // Helper to convert ChunkCoord to map key
    static uint64_t coordToKey(const ChunkCoord& coord) {
        return coord.hash();
    }
};

} // namespace voxel
} // namespace jupiter
