#pragma once

/**
 * @file voxel_command.h
 * @brief Voxel edit command types for Aeron-style command queue
 *
 * Commands are the single-writer interface for voxel modifications.
 * All edits go through the command queue for lock-free processing.
 */

#include "voxel_types.h"
#include <cstdint>

namespace jupiter {
namespace voxel {

// ============================================================================
// Command Types
// ============================================================================

/**
 * @brief Types of voxel commands
 */
enum class VoxelCommandType : uint8_t {
    None = 0,           ///< Invalid/empty command
    SetBlock,           ///< Single block edit
    SetBlockBatch,      ///< Multiple block edits (uses external buffer)
    FillBox,            ///< Fill AABB with block type
    CSGUnion,           ///< Add CSG shape (union)
    CSGDifference,      ///< Subtract CSG shape (difference)
    CSGIntersection,    ///< Intersect with CSG shape
    CSGReplace,         ///< Replace with CSG shape material
    GenerateTerrain,    ///< Procedural terrain generation
    InvalidateChunk,    ///< Mark chunk for re-mesh
    UnloadChunk,        ///< Request chunk unload
};

/**
 * @brief Command flags
 */
enum class VoxelCommandFlags : uint16_t {
    None = 0,
    HighPriority = 1 << 0,      ///< Process before normal priority
    SkipMesh = 1 << 1,          ///< Don't trigger re-mesh after edit
    Async = 1 << 2,             ///< Can be processed on worker thread
    Immediate = 1 << 3,         ///< Process immediately, skip batching
};

inline VoxelCommandFlags operator|(VoxelCommandFlags a, VoxelCommandFlags b) {
    return static_cast<VoxelCommandFlags>(
        static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline bool hasFlag(VoxelCommandFlags flags, VoxelCommandFlags flag) {
    return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

// ============================================================================
// Command Data Structures
// ============================================================================

/**
 * @brief Data for SetBlock command
 */
struct SetBlockData {
    int32_t x;
    int32_t y;
    int32_t z;
    BlockType block;
    uint8_t padding[3];
};
static_assert(sizeof(SetBlockData) == 16, "SetBlockData must be 16 bytes");

/**
 * @brief Data for SetBlockBatch command
 */
struct SetBlockBatchData {
    const int32_t* positions;  ///< Pointer to xyz positions (count * 3 ints)
    const BlockType* blocks;   ///< Pointer to block types (count)
    uint32_t count;
    uint32_t padding;
};
static_assert(sizeof(SetBlockBatchData) == 24, "SetBlockBatchData must be 24 bytes");

/**
 * @brief Data for FillBox command
 */
struct FillBoxData {
    int32_t minX, minY, minZ;
    int32_t maxX, maxY, maxZ;
    BlockType block;
    uint8_t padding[3];
};
static_assert(sizeof(FillBoxData) == 28, "FillBoxData must be 28 bytes");

/**
 * @brief Data for CSG commands
 */
struct CSGCommandData {
    uint32_t primitiveIndex;   ///< Index into CSG primitive pool
    uint32_t reserved;
};
static_assert(sizeof(CSGCommandData) == 8, "CSGCommandData must be 8 bytes");

/**
 * @brief Data for GenerateTerrain command
 */
struct TerrainGenData {
    int32_t chunkX;
    int32_t chunkY;
    int32_t chunkZ;
    uint32_t seed;
    uint32_t generatorType;    ///< Which terrain generator to use
    
    ChunkCoord toCoord() const { return ChunkCoord{chunkX, chunkY, chunkZ}; }
};
static_assert(sizeof(TerrainGenData) == 20, "TerrainGenData must be 20 bytes");

/**
 * @brief Data for chunk operations
 */
struct ChunkOpData {
    int32_t chunkX;
    int32_t chunkY;
    int32_t chunkZ;
    
    ChunkCoord toCoord() const { return ChunkCoord{chunkX, chunkY, chunkZ}; }
};
static_assert(sizeof(ChunkOpData) == 12, "ChunkOpData must be 12 bytes");

// ============================================================================
// Voxel Command
// ============================================================================

/**
 * @brief Single voxel command (32 bytes, cache-line friendly)
 *
 * Commands are queued via SPSC queue and processed by single writer.
 * Sequence number enables Aeron-style ordering and acknowledgement.
 */
struct alignas(32) VoxelCommand {
    VoxelCommandType type;      ///< Command type
    uint8_t priority;           ///< Higher = process first (0-255)
    VoxelCommandFlags flags;    ///< Command flags
    
    /// Command-specific data (24 bytes max)
    union {
        SetBlockData setBlock;
        FillBoxData fillBox;
        CSGCommandData csg;
        TerrainGenData terrain;
        ChunkOpData chunkOp;
        uint8_t raw[24];
    } data;
    
    uint64_t sequenceNumber;    ///< Aeron-style sequence for ordering
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    /**
     * @brief Create a SetBlock command
     */
    static VoxelCommand setBlock(int32_t x, int32_t y, int32_t z, BlockType block) {
        VoxelCommand cmd{};
        cmd.type = VoxelCommandType::SetBlock;
        cmd.priority = 128;
        cmd.flags = VoxelCommandFlags::None;
        cmd.data.setBlock.x = x;
        cmd.data.setBlock.y = y;
        cmd.data.setBlock.z = z;
        cmd.data.setBlock.block = block;
        return cmd;
    }
    
    /**
     * @brief Create a FillBox command
     */
    static VoxelCommand fillBox(int32_t minX, int32_t minY, int32_t minZ,
                                int32_t maxX, int32_t maxY, int32_t maxZ,
                                BlockType block) {
        VoxelCommand cmd{};
        cmd.type = VoxelCommandType::FillBox;
        cmd.priority = 128;
        cmd.flags = VoxelCommandFlags::None;
        cmd.data.fillBox.minX = minX;
        cmd.data.fillBox.minY = minY;
        cmd.data.fillBox.minZ = minZ;
        cmd.data.fillBox.maxX = maxX;
        cmd.data.fillBox.maxY = maxY;
        cmd.data.fillBox.maxZ = maxZ;
        cmd.data.fillBox.block = block;
        return cmd;
    }
    
    /**
     * @brief Create a CSG command
     */
    static VoxelCommand csg(VoxelCommandType csgType, uint32_t primitiveIndex) {
        VoxelCommand cmd{};
        cmd.type = csgType;
        cmd.priority = 128;
        cmd.flags = VoxelCommandFlags::None;
        cmd.data.csg.primitiveIndex = primitiveIndex;
        return cmd;
    }
    
    /**
     * @brief Create a terrain generation command
     */
    static VoxelCommand generateTerrain(const ChunkCoord& coord, uint32_t seed,
                                        uint32_t generatorType = 0) {
        VoxelCommand cmd{};
        cmd.type = VoxelCommandType::GenerateTerrain;
        cmd.priority = 64;  // Lower priority than edits
        cmd.flags = VoxelCommandFlags::Async;
        cmd.data.terrain.chunkX = coord.x;
        cmd.data.terrain.chunkY = coord.y;
        cmd.data.terrain.chunkZ = coord.z;
        cmd.data.terrain.seed = seed;
        cmd.data.terrain.generatorType = generatorType;
        return cmd;
    }
    
    /**
     * @brief Create an invalidate chunk command
     */
    static VoxelCommand invalidateChunk(const ChunkCoord& coord) {
        VoxelCommand cmd{};
        cmd.type = VoxelCommandType::InvalidateChunk;
        cmd.priority = 192;  // High priority for mesh updates
        cmd.flags = VoxelCommandFlags::None;
        cmd.data.chunkOp.chunkX = coord.x;
        cmd.data.chunkOp.chunkY = coord.y;
        cmd.data.chunkOp.chunkZ = coord.z;
        return cmd;
    }
    
    /**
     * @brief Create an unload chunk command
     */
    static VoxelCommand unloadChunk(const ChunkCoord& coord) {
        VoxelCommand cmd{};
        cmd.type = VoxelCommandType::UnloadChunk;
        cmd.priority = 32;  // Low priority
        cmd.flags = VoxelCommandFlags::None;
        cmd.data.chunkOp.chunkX = coord.x;
        cmd.data.chunkOp.chunkY = coord.y;
        cmd.data.chunkOp.chunkZ = coord.z;
        return cmd;
    }
    
    // ========================================================================
    // Utilities
    // ========================================================================
    
    /**
     * @brief Check if command is valid
     */
    bool isValid() const {
        return type != VoxelCommandType::None;
    }
    
    /**
     * @brief Set high priority
     */
    VoxelCommand& withHighPriority() {
        priority = 255;
        flags = flags | VoxelCommandFlags::HighPriority;
        return *this;
    }
    
    /**
     * @brief Set skip mesh flag
     */
    VoxelCommand& withSkipMesh() {
        flags = flags | VoxelCommandFlags::SkipMesh;
        return *this;
    }
};

// VoxelCommand is 64 bytes with alignment for cache-friendly queuing
static_assert(sizeof(VoxelCommand) <= 64, "VoxelCommand should fit in cache line");
static_assert(alignof(VoxelCommand) == 32, "VoxelCommand must be 32-byte aligned");

} // namespace voxel
} // namespace jupiter

