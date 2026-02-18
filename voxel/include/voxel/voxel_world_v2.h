#pragma once

/**
 * @file voxel_world_v2.h
 * @brief Enhanced VoxelWorld with kernel-based meshing and command queue
 *
 * Provides the same interface as VoxelWorld but uses:
 * - Command queue for all modifications (Aeron-style)
 * - Kernel-based meshing pipeline
 * - Columnar chunk storage
 * - CSG support
 */

#include "voxel_world.h"
#include "voxel_command_queue.h"
#include "voxel_kernel_registry.h"
#include "voxel_kernels.h"
#include "voxel_column.h"
#include "chunk_columns_pool.h"
#include "csg_evaluator.h"
#include "mesh_buffer.h"
#include "mesh_mode.h"
#include "smooth_mesh_kernel.h"
#include "block_registry.h"
#include "terrain_rules.h"
#include "voxel_types.h"
#include <unordered_map>

namespace jupiter {
namespace voxel {

/**
 * @brief Configuration for VoxelWorldV2
 */
struct VoxelWorldV2Config : public VoxelWorldConfig {
    bool useKernelMeshing = true;           ///< Use kernel-based meshing
    bool useColumnarStorage = true;         ///< Use columnar chunk storage
    size_t commandQueueCapacity = 4096;     ///< Command queue capacity
    
    /// Mesh mode (Blocky, Smooth, Hybrid)
    MeshMode meshMode = MeshMode::Blocky;
    
    /// Mesh configuration for smooth terrain
    MeshConfig meshConfig;
    
    /// Path to blocks.json (optional)
    std::string blocksJsonPath;
    
    /// Path to terrain_rules.json (optional)
    std::string terrainJsonPath;
};

/**
 * @brief Callback for new kernel-based mesh output
 */
using KernelMeshBufferCallback = std::function<void(
    const ChunkCoord& coord,
    uint32_t poolIndex,
    const KernelMeshBuffer& buffer
)>;

/**
 * @brief Callback for smooth mesh output
 */
using SmoothMeshCallback = std::function<void(
    const ChunkCoord& coord,
    const SmoothMeshBuffer& buffer
)>;

/**
 * @brief Callback for custom terrain generation
 *
 * Called when a chunk needs terrain generated.
 * Implement this to provide custom noise-based or procedural terrain.
 *
 * @param chunk The chunk columns to fill
 * @param coord The chunk coordinate
 */
using TerrainGeneratorCallback = std::function<void(
    ChunkColumns& chunk,
    const ChunkCoord& coord
)>;

/**
 * @brief Enhanced VoxelWorld with command queue and kernel processing
 *
 * This version adds:
 * - VoxelCommandQueue for all modifications
 * - Kernel-based meshing (FaceCuller -> AO -> GreedyMesh -> Encode)
 * - CSG operations (union, difference, intersection)
 * - Columnar storage for better cache performance
 *
 * The original API is preserved for backward compatibility.
 */
class VoxelWorldV2 {
public:
    VoxelWorldV2() = default;
    ~VoxelWorldV2();
    
    // Non-copyable, non-movable
    VoxelWorldV2(const VoxelWorldV2&) = delete;
    VoxelWorldV2& operator=(const VoxelWorldV2&) = delete;
    VoxelWorldV2(VoxelWorldV2&&) = delete;
    VoxelWorldV2& operator=(VoxelWorldV2&&) = delete;
    
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * @brief Initialize the voxel world
     */
    bool initialize(const VoxelWorldV2Config& config = {});
    
    /**
     * @brief Shutdown the voxel world
     */
    void shutdown();
    
    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    /**
     * @brief Set callback for legacy mesh format
     */
    void setMeshCallback(ChunkMeshCallback callback) {
        meshCallback_ = std::move(callback);
    }
    
    /**
     * @brief Set callback for raw mesh format
     */
    void setRawMeshCallback(RawMeshCallback callback) {
        rawMeshCallback_ = std::move(callback);
    }
    
    /**
     * @brief Set callback for new KernelMeshBuffer format
     */
    void setKernelMeshBufferCallback(KernelMeshBufferCallback callback) {
        kernelMeshBufferCallback_ = std::move(callback);
    }
    
    /**
     * @brief Set callback for chunk unload
     */
    void setUnloadCallback(ChunkUnloadCallback callback) {
        unloadCallback_ = std::move(callback);
    }
    
    /**
     * @brief Set callback for smooth mesh output
     */
    void setSmoothMeshCallback(SmoothMeshCallback callback) {
        smoothMeshCallback_ = std::move(callback);
    }
    
    /**
     * @brief Set custom terrain generator
     *
     * If set, this callback will be used instead of built-in terrain generation.
     * The callback should fill both blocks and density fields of the chunk.
     */
    void setTerrainGenerator(TerrainGeneratorCallback callback) {
        terrainGenerator_ = std::move(callback);
    }
    
    // ========================================================================
    // Mesh Mode Control
    // ========================================================================
    
    /**
     * @brief Set mesh mode (Blocky, Smooth, Hybrid)
     */
    void setMeshMode(MeshMode mode) {
        config_.meshMode = mode;
    }
    
    /**
     * @brief Get current mesh mode
     */
    MeshMode getMeshMode() const {
        return config_.meshMode;
    }
    
    /**
     * @brief Set mesh configuration
     */
    void setMeshConfig(const MeshConfig& config) {
        config_.meshConfig = config;
    }
    
    // ========================================================================
    // Per-Frame Update
    // ========================================================================
    
    /**
     * @brief Update the voxel world
     *
     * @param cameraPos Camera world position
     * @param deltaTime Frame delta time in seconds
     */
    void update(const glm::vec3& cameraPos, float deltaTime);
    
    // ========================================================================
    // Command Queue Interface (New)
    // ========================================================================
    
    /**
     * @brief Enqueue a voxel command
     */
    bool enqueueCommand(const VoxelCommand& cmd) {
        return commandQueue_.enqueue(cmd);
    }
    
    /**
     * @brief Set a single block via command queue
     */
    bool setBlockQueued(int32_t x, int32_t y, int32_t z, BlockType block) {
        return commandQueue_.enqueue(VoxelCommand::setBlock(x, y, z, block));
    }
    
    /**
     * @brief Fill a box with a block type via command queue
     */
    bool fillBoxQueued(int32_t minX, int32_t minY, int32_t minZ,
                       int32_t maxX, int32_t maxY, int32_t maxZ,
                       BlockType block) {
        return commandQueue_.enqueue(
            VoxelCommand::fillBox(minX, minY, minZ, maxX, maxY, maxZ, block));
    }
    
    /**
     * @brief Apply CSG operation via command queue
     */
    bool applyCSG(const CSGPrimitive& primitive) {
        uint32_t idx = csgPool_.add(primitive);
        if (idx == UINT32_MAX) return false;
        
        VoxelCommandType type;
        switch (primitive.operation) {
            case CSGOperation::Union:
                type = VoxelCommandType::CSGUnion;
                break;
            case CSGOperation::Difference:
                type = VoxelCommandType::CSGDifference;
                break;
            case CSGOperation::Intersection:
                type = VoxelCommandType::CSGIntersection;
                break;
            default:
                type = VoxelCommandType::CSGUnion;
                break;
        }
        
        return commandQueue_.enqueue(VoxelCommand::csg(type, idx));
    }
    
    /**
     * @brief Get pending command count
     */
    uint64_t getPendingCommandCount() const {
        return commandQueue_.pendingCount();
    }
    
    // ========================================================================
    // Legacy Block Access (Immediate)
    // ========================================================================
    
    /**
     * @brief Get block at world position
     */
    BlockType getBlock(const glm::ivec3& worldPos) const;
    
    /**
     * @brief Set block at world position (immediate, for compatibility)
     */
    bool setBlock(const glm::ivec3& worldPos, BlockType type);
    
    /**
     * @brief Check if block is solid
     */
    bool isSolid(const glm::ivec3& worldPos) const;
    
    // ========================================================================
    // Raycast
    // ========================================================================
    
    /**
     * @brief Cast ray against voxel world
     */
    VoxelRaycastResult raycast(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float maxDistance) const;
    
    // ========================================================================
    // Chunk Access
    // ========================================================================
    
    bool isChunkLoaded(const ChunkCoord& coord) const;
    uint32_t getChunkIndex(const ChunkCoord& coord) const;
    const ChunkVoxelData* getChunkData(const ChunkCoord& coord) const;
    uint32_t loadChunk(const ChunkCoord& coord);
    void unloadChunk(const ChunkCoord& coord);
    
    /**
     * @brief Get columnar chunk data
     */
    const ChunkColumns* getChunkColumns(const ChunkCoord& coord) const;
    
    // ========================================================================
    // LOD Management
    // ========================================================================
    
    /**
     * @brief Set LOD level for a chunk
     */
    void setChunkLOD(const ChunkCoord& coord, LODLevel lod);
    
    /**
     * @brief Get LOD level for a chunk
     */
    LODLevel getChunkLOD(const ChunkCoord& coord) const;
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    uint32_t getLoadedChunkCount() const;
    uint32_t getPendingMeshCount() const;
    uint32_t getChunksMeshedThisFrame() const;
    uint32_t getCommandsProcessedThisFrame() const { return commandsProcessedThisFrame_; }
    
    const VoxelWorldV2Config& getConfig() const { return config_; }
    
private:
    // Process pending commands from queue
    void processCommands();
    
    // Apply a single command
    void applyCommand(const VoxelCommand& cmd);
    
    // Generate terrain using kernel
    void generateChunkTerrainKernel(ChunkColumns* chunk, const ChunkCoord& coord);
    
    // Mesh a chunk using kernel pipeline (blocky)
    void meshChunkKernel(const ChunkCoord& coord, uint32_t poolIndex);
    
    // Mesh a chunk using smooth meshing (Marching Cubes + Transvoxel)
    void meshChunkSmooth(const ChunkCoord& coord);
    
    // Get neighbor columns for meshing
    void getNeighborColumns(const ChunkCoord& coord,
                            const ChunkColumns* neighbors[6]) const;
    
    // Generate density values for smooth terrain
    void generateChunkDensity(ChunkColumns* chunk, const ChunkCoord& coord);
    
    VoxelWorldV2Config config_;
    bool initialized_ = false;
    
    // Command queue
    VoxelCommandQueue commandQueue_;
    
    // CSG primitive pool
    CSGPrimitivePool csgPool_;
    
    // CSG evaluator
    CSGEvaluator csgEvaluator_;
    
    // Pre-allocated pool for ChunkColumns (O(1) acquire/release)
    ChunkColumnsPool chunkColumnsPool_;
    
    // Mapping from pool index to ChunkColumns pointer (sparse)
    std::vector<ChunkColumns*> chunkColumnsMap_;
    
    // Optional density storage (only for smooth mode)
    // Using vector of unique_ptr for now - can be pooled later if needed
    std::vector<std::unique_ptr<ChunkDensity>> densityStorage_;
    
    // Mesh buffer pool for kernel output
    KernelMeshBufferPool kernelMeshPool_;
    
    // Subsystems from original VoxelWorld
    std::unique_ptr<ChunkPool> chunkPool_;
    std::unique_ptr<ChunkMap> chunkMap_;
    std::unique_ptr<DirtyChunkQueue> dirtyQueue_;
    std::unique_ptr<StreamingManager> streamingManager_;
    std::unique_ptr<MeshingBudgeter> meshingBudgeter_;
    
    // Callbacks
    ChunkMeshCallback meshCallback_;
    RawMeshCallback rawMeshCallback_;
    KernelMeshBufferCallback kernelMeshBufferCallback_;
    ChunkUnloadCallback unloadCallback_;
    SmoothMeshCallback smoothMeshCallback_;
    TerrainGeneratorCallback terrainGenerator_;
    
    // Smooth mesh kernel
    SmoothMeshKernel smoothMeshKernel_;
    
    // LOD tracking per chunk
    std::unordered_map<ChunkCoord, LODLevel, ChunkCoordHash> chunkLODs_;
    
    // Per-frame stats
    uint32_t chunksMeshedThisFrame_ = 0;
    uint32_t commandsProcessedThisFrame_ = 0;
};

} // namespace voxel
} // namespace jupiter

