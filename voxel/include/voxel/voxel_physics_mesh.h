#pragma once

/**
 * @file voxel_physics_mesh.h
 * @brief Collision mesh generation from voxel chunks
 *
 * Generates triangle soup collision meshes from voxel data.
 * The output format is designed for physics engines.
 *
 * Following Project Jupiter principles:
 * - Uses pooled buffers (no runtime allocation)
 * - Simplified output (position-only triangles)
 * - Compatible with any physics library
 */

#include "voxel_types.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace jupiter {
namespace voxel {

/**
 * @brief Simple triangle for collision mesh
 *
 * Position-only, no normals or UVs needed for physics.
 */
struct PhysicsTriangle {
    glm::vec3 v0;
    glm::vec3 v1;
    glm::vec3 v2;
};

static_assert(sizeof(PhysicsTriangle) == 36, "PhysicsTriangle should be 36 bytes");

/**
 * @brief Axis-aligned bounding box
 */
struct PhysicsAABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return max - min; }
};

/**
 * @brief Result of physics mesh generation
 */
struct PhysicsMeshResult {
    bool success = false;
    uint32_t triangleCount = 0;
    PhysicsAABB aabb;
};

/**
 * @brief Generates collision meshes from voxel chunks
 *
 * Creates simplified triangle soup suitable for physics collision detection.
 * Unlike render meshes, physics meshes:
 * - Are lower resolution (can use box collision per solid voxel or merged faces)
 * - Only contain position data (no normals, UVs)
 * - Can be regenerated less frequently than render meshes
 *
 * Thread-safety: Each thread should have its own VoxelPhysicsMesh instance.
 */
class VoxelPhysicsMesh {
public:
    /// Maximum triangles per chunk (2 per face, 6 faces per voxel, but heavily merged)
    static constexpr uint32_t MAX_TRIANGLES_PER_CHUNK = 16384;

    /// Maximum vertices for box mode (8 per solid voxel, simplified)
    static constexpr uint32_t MAX_VERTICES_PER_CHUNK = MAX_TRIANGLES_PER_CHUNK * 3;

    VoxelPhysicsMesh();
    ~VoxelPhysicsMesh();

    // Non-copyable
    VoxelPhysicsMesh(const VoxelPhysicsMesh&) = delete;
    VoxelPhysicsMesh& operator=(const VoxelPhysicsMesh&) = delete;

    /**
     * @brief Generate collision mesh from raw voxel data
     *
     * Uses greedy face merging for efficiency - adjacent faces of same
     * block type are merged into larger quads.
     *
     * @param chunk Chunk voxel data
     * @param chunkCoord World chunk coordinate
     * @param outTriangles Output triangle buffer (must have space for MAX_TRIANGLES_PER_CHUNK)
     * @return Result with triangle count and AABB
     */
    PhysicsMeshResult generateFromVoxelData(
        const ChunkVoxelData* chunk,
        const ChunkCoord& chunkCoord,
        PhysicsTriangle* outTriangles);

    /**
     * @brief Generate collision mesh from stb_voxel_render output
     *
     * Converts render mesh quads directly to physics triangles.
     * Faster than regenerating from voxel data.
     *
     * @param stbVertices Raw stb_voxel_render vertex data (Mode 30)
     * @param numVertices Number of vertices (must be multiple of 4)
     * @param scale Scale factor from stb_voxel_render
     * @param chunkWorldPos Chunk world position offset
     * @param outTriangles Output triangle buffer
     * @return Result with triangle count and AABB
     */
    PhysicsMeshResult generateFromStbMesh(
        const void* stbVertices,
        uint32_t numVertices,
        const glm::vec3& scale,
        const glm::vec3& chunkWorldPos,
        PhysicsTriangle* outTriangles);

    /**
     * @brief Generate simplified box colliders for chunk
     *
     * Instead of per-face triangles, generates axis-aligned boxes
     * for each solid voxel. More efficient for some physics engines.
     *
     * @param chunk Chunk voxel data
     * @param chunkCoord World chunk coordinate
     * @param outBoxes Output AABB buffer (one per solid voxel, max CHUNK_SIZE^3)
     * @param maxBoxes Maximum number of boxes to write
     * @return Number of boxes written
     */
    uint32_t generateBoxColliders(
        const ChunkVoxelData* chunk,
        const ChunkCoord& chunkCoord,
        PhysicsAABB* outBoxes,
        uint32_t maxBoxes);

private:
    /**
     * @brief Check if a block is solid (blocks player movement)
     */
    bool isSolid(BlockType type) const;

    /**
     * @brief Check if a face should be generated between two blocks
     */
    bool shouldGenerateFace(BlockType solid, BlockType neighbor) const;

    /**
     * @brief Add quad as two triangles to output
     */
    void addQuadTriangles(
        const glm::vec3& v0, const glm::vec3& v1,
        const glm::vec3& v2, const glm::vec3& v3,
        PhysicsTriangle* outTriangles,
        uint32_t& triangleIndex);

    /**
     * @brief Unpack position from stb_voxel_render Mode 30 vertex
     *
     * @param attrVertex The attr_vertex field from stb format
     * @param scale Scale factor from stb_voxel_render
     * @return Local position in chunk
     */
    glm::vec3 unpackStbPosition(uint32_t attrVertex, const glm::vec3& scale) const;
};

/**
 * @brief Pool of physics mesh buffers to avoid runtime allocation
 */
class PhysicsMeshPool {
public:
    PhysicsMeshPool() = default;
    ~PhysicsMeshPool();

    // Non-copyable
    PhysicsMeshPool(const PhysicsMeshPool&) = delete;
    PhysicsMeshPool& operator=(const PhysicsMeshPool&) = delete;

    /**
     * @brief Initialize pool with pre-allocated buffers
     *
     * @param numBuffers Number of triangle buffers to allocate
     * @return true if successful
     */
    bool initialize(uint32_t numBuffers);

    /**
     * @brief Shutdown and free all buffers
     */
    void shutdown();

    /**
     * @brief Acquire a triangle buffer from pool
     *
     * @return Pointer to buffer (MAX_TRIANGLES_PER_CHUNK capacity), or nullptr if exhausted
     */
    PhysicsTriangle* acquireBuffer();

    /**
     * @brief Release a buffer back to pool
     */
    void releaseBuffer(PhysicsTriangle* buffer);

    /**
     * @brief Get number of available buffers
     */
    uint32_t getAvailableCount() const;

private:
    std::vector<PhysicsTriangle*> buffers_;
    std::vector<bool> inUse_;
    uint32_t numBuffers_ = 0;
    bool initialized_ = false;
};

} // namespace voxel
} // namespace jupiter
