#pragma once

#include <cstdint>
#include <glm/glm.hpp>

/**
 * @file voxel_types.h
 * @brief Core voxel types and constants
 *
 * Following Project Jupiter principles:
 * - No runtime allocations in hot paths
 * - Cache-aligned data structures (64 bytes)
 * - Lock-free design compatible
 */

namespace jupiter {
namespace voxel {

// ============================================================================
// Constants
// ============================================================================

/// Voxels per chunk dimension (16x16x16 cube chunks)
constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_HEIGHT = 16;  // Cube chunks for better culling and fewer visible chunks

/// Border size for stb_voxel_render neighbor access
constexpr int CHUNK_BORDER = 1;

/// Padded chunk dimensions including borders
constexpr int PADDED_SIZE = CHUNK_SIZE + 2 * CHUNK_BORDER;    // 18 for X/Z
constexpr int PADDED_HEIGHT = CHUNK_HEIGHT + 2 * CHUNK_BORDER; // 130 for Y

/// Total voxels in padded chunk data (18 * 130 * 18 = 42,120)
constexpr int VOXEL_DATA_SIZE = PADDED_SIZE * PADDED_HEIGHT * PADDED_SIZE;

/// Maximum active chunks in memory
constexpr size_t MAX_ACTIVE_CHUNKS = 8192;

/// Default view distance in chunks
constexpr int DEFAULT_VIEW_DISTANCE = 12;

/// Maximum vertices per chunk mesh
constexpr uint32_t CHUNK_MAX_VERTICES = 32768;

/// Gigabuffer size for all voxel geometry (256MB)
constexpr size_t GIGABUFFER_SIZE = 256 * 1024 * 1024;

// ============================================================================
// Block Types
// ============================================================================

/// Block type identifier (256 possible block types)
using BlockType = uint8_t;

/// Air block (empty/transparent)
constexpr BlockType BLOCK_AIR = 0;

/// Stone block
constexpr BlockType BLOCK_STONE = 1;

/// Dirt block
constexpr BlockType BLOCK_DIRT = 2;

/// Grass block
constexpr BlockType BLOCK_GRASS = 3;

/// Sand block
constexpr BlockType BLOCK_SAND = 4;

/// Water block
constexpr BlockType BLOCK_WATER = 5;

/// Wood block
constexpr BlockType BLOCK_WOOD = 6;

/// Leaves block
constexpr BlockType BLOCK_LEAVES = 7;

// ============================================================================
// Chunk State
// ============================================================================

/// State machine for chunk lifecycle
enum class ChunkState : uint8_t {
    Unloaded = 0,   ///< Not in memory
    Loading,        ///< Being generated/loaded from disk
    Loaded,         ///< Block data ready, mesh not built
    Meshing,        ///< stb_voxel_render meshing in progress
    Ready,          ///< Mesh uploaded, renderable
    NeedsMesh,      ///< Blocks edited, mesh outdated
    Unloading       ///< Being released
};

// ============================================================================
// Chunk Coordinate
// ============================================================================

/// 3D chunk coordinate in chunk space (not world space)
struct ChunkCoord {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    ChunkCoord() = default;
    ChunkCoord(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const ChunkCoord& other) const {
        return !(*this == other);
    }

    /// Convert to world position (chunk origin)
    glm::vec3 toWorldPos() const {
        return glm::vec3(
            static_cast<float>(x * CHUNK_SIZE),
            static_cast<float>(y * CHUNK_SIZE),
            static_cast<float>(z * CHUNK_SIZE)
        );
    }

    /// Hash function for use in hash maps
    uint64_t hash() const {
        // Morton code for spatial locality
        uint64_t hx = static_cast<uint32_t>(x) & 0x1FFFFF;
        uint64_t hy = static_cast<uint32_t>(y) & 0x1FFFFF;
        uint64_t hz = static_cast<uint32_t>(z) & 0x1FFFFF;
        return (hx * 73856093ULL) ^ (hy * 19349663ULL) ^ (hz * 83492791ULL);
    }
};

/// Hash functor for ChunkCoord
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& coord) const {
        return static_cast<size_t>(coord.hash());
    }
};

// ============================================================================
// Chunk Voxel Data
// ============================================================================

/**
 * @brief CPU-side voxel data for a chunk
 *
 * Includes border voxels for stb_voxel_render neighbor access.
 * Total size: ~12KB per chunk
 *
 * Memory layout follows stb_voxel_render expectations:
 * - Z varies fastest (stride 1) - REQUIRED by stb_voxel_render
 * - Y varies second (stride PADDED_SIZE)
 * - X varies slowest (stride PADDED_SIZE * PADDED_SIZE)
 */
struct alignas(64) ChunkVoxelData {
    /// Block type for each voxel (including borders)
    BlockType blocks[VOXEL_DATA_SIZE];

    /// Lighting/AO data for each voxel
    uint8_t lighting[VOXEL_DATA_SIZE];

    /// Edit generation counter (incremented on any modification)
    uint64_t editGeneration = 0;

    /// Get linear index from local coordinates (with border offset)
    /// Memory layout: Z varies fastest (stride 1), Y second (stride 18), X slowest (stride 324)
    static constexpr int getIndex(int x, int y, int z) {
        // Add border offset (1,1,1) to convert from chunk-local to padded coords
        const int px = x + CHUNK_BORDER;
        const int py = y + CHUNK_BORDER;
        const int pz = z + CHUNK_BORDER;
        // Z varies fastest for stb_voxel_render compatibility
        return pz + py * PADDED_SIZE + px * PADDED_SIZE * PADDED_SIZE;
    }

    /// Get block at local coordinates (0 to CHUNK_SIZE-1)
    BlockType getBlock(int x, int y, int z) const {
        return blocks[getIndex(x, y, z)];
    }

    /// Set block at local coordinates (0 to CHUNK_SIZE-1)
    void setBlock(int x, int y, int z, BlockType type) {
        blocks[getIndex(x, y, z)] = type;
        editGeneration++;
    }

    /// Fill entire chunk with a block type
    void fill(BlockType type) {
        for (int i = 0; i < VOXEL_DATA_SIZE; ++i) {
            blocks[i] = type;
        }
        editGeneration++;
    }

    /// Clear chunk to air
    void clear() {
        fill(BLOCK_AIR);
    }
};

static_assert(sizeof(ChunkVoxelData) % 64 == 0, "ChunkVoxelData must be 64-byte aligned");

// ============================================================================
// Voxel Vertex Format
// ============================================================================

/**
 * @brief Compressed vertex format for voxel meshes (12 bytes)
 *
 * Derived from stb_voxel_render Mode 30 output, adapted for PBR.
 * Compatible with Ascendant-style gigabuffer pattern.
 */
struct VoxelVertex {
    /// Chunk-local position (unorm16, 0-65535 maps to 0-16)
    uint16_t position[3];

    /// Packed: bits 0-2 = normal index (0-5), bits 3-7 = AO (0-31), bits 8-15 = flags
    uint16_t normalAO;

    /// Material index (block type for texture lookup)
    uint16_t materialIndex;

    /// Packed UV: bits 0-7 = U, bits 8-15 = V
    uint16_t packedUV;
};

static_assert(sizeof(VoxelVertex) == 12, "VoxelVertex must be 12 bytes");

// ============================================================================
// GPU Chunk Info (for compute culling)
// ============================================================================

/**
 * @brief GPU-side chunk metadata for culling and indirect drawing
 */
struct GPUChunkInfo {
    int32_t chunkX, chunkY, chunkZ;   // 12 bytes
    uint32_t gigabufferOffset;         // 16 bytes - Byte offset in gigabuffer
    uint32_t vertexCount;              // 20 bytes - Number of vertices
    uint32_t flags;                    // 24 bytes - Visible, LOD level, etc.
    float aabbMinX, aabbMinY, aabbMinZ; // 36 bytes
    float aabbMaxX, aabbMaxY, aabbMaxZ; // 48 bytes
};

static_assert(sizeof(GPUChunkInfo) == 48, "GPUChunkInfo must be 48 bytes");

// ============================================================================
// Indirect Draw Command (extended for voxels)
// ============================================================================

/**
 * @brief Indirect draw command with embedded chunk position
 *
 * Standard VkDrawIndirectCommand followed by chunk position for shader access.
 */
struct VoxelDrawIndirect {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
    int32_t chunkX, chunkY, chunkZ;
    uint32_t padding;
};

static_assert(sizeof(VoxelDrawIndirect) == 32, "VoxelDrawIndirect must be 32 bytes");

// ============================================================================
// Utility Functions
// ============================================================================

/// Convert world position to chunk coordinate
inline ChunkCoord worldToChunk(const glm::vec3& worldPos) {
    return ChunkCoord(
        static_cast<int32_t>(floorf(worldPos.x / CHUNK_SIZE)),
        static_cast<int32_t>(floorf(worldPos.y / CHUNK_SIZE)),
        static_cast<int32_t>(floorf(worldPos.z / CHUNK_SIZE))
    );
}

/// Convert world position to local chunk position (0 to CHUNK_SIZE-1)
inline glm::ivec3 worldToLocal(const glm::vec3& worldPos) {
    int lx = static_cast<int>(floorf(worldPos.x)) % CHUNK_SIZE;
    int ly = static_cast<int>(floorf(worldPos.y)) % CHUNK_SIZE;
    int lz = static_cast<int>(floorf(worldPos.z)) % CHUNK_SIZE;
    // Handle negative coordinates
    if (lx < 0) lx += CHUNK_SIZE;
    if (ly < 0) ly += CHUNK_SIZE;
    if (lz < 0) lz += CHUNK_SIZE;
    return glm::ivec3(lx, ly, lz);
}

/// Calculate squared distance between chunk and point
inline float chunkDistanceSq(const ChunkCoord& chunk, const glm::vec3& point) {
    glm::vec3 chunkCenter = chunk.toWorldPos() + glm::vec3(CHUNK_SIZE * 0.5f);
    glm::vec3 diff = chunkCenter - point;
    return glm::dot(diff, diff);
}

} // namespace voxel
} // namespace jupiter
