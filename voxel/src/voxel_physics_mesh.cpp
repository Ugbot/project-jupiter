/**
 * @file voxel_physics_mesh.cpp
 * @brief Implementation of collision mesh generation from voxel chunks
 */

#include "voxel/voxel_physics_mesh.h"
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace voxel {

// ============================================================================
// VoxelPhysicsMesh
// ============================================================================

VoxelPhysicsMesh::VoxelPhysicsMesh() = default;
VoxelPhysicsMesh::~VoxelPhysicsMesh() = default;

bool VoxelPhysicsMesh::isSolid(BlockType type) const {
    // Air and water are non-solid
    return type != BLOCK_AIR && type != BLOCK_WATER;
}

bool VoxelPhysicsMesh::shouldGenerateFace(BlockType solid, BlockType neighbor) const {
    // Generate face if solid voxel is adjacent to non-solid
    return isSolid(solid) && !isSolid(neighbor);
}

void VoxelPhysicsMesh::addQuadTriangles(
    const glm::vec3& v0, const glm::vec3& v1,
    const glm::vec3& v2, const glm::vec3& v3,
    PhysicsTriangle* outTriangles,
    uint32_t& triangleIndex)
{
    // Triangle 1: v0, v1, v2
    outTriangles[triangleIndex].v0 = v0;
    outTriangles[triangleIndex].v1 = v1;
    outTriangles[triangleIndex].v2 = v2;
    triangleIndex++;

    // Triangle 2: v0, v2, v3
    outTriangles[triangleIndex].v0 = v0;
    outTriangles[triangleIndex].v1 = v2;
    outTriangles[triangleIndex].v2 = v3;
    triangleIndex++;
}

glm::vec3 VoxelPhysicsMesh::unpackStbPosition(uint32_t attrVertex, const glm::vec3& scale) const {
    // stb_voxel_render Mode 30 position encoding:
    // Bits 0-7: X position (0-255 scaled)
    // Bits 8-15: Y position (0-255 scaled)
    // Bits 16-22: Z position (0-127 scaled)
    // Bits 23+: AO, texlerp, etc.

    float x = static_cast<float>(attrVertex & 0xFF);
    float y = static_cast<float>((attrVertex >> 8) & 0xFF);
    float z = static_cast<float>((attrVertex >> 16) & 0x7F);

    return glm::vec3(x * scale.x, y * scale.y, z * scale.z);
}

PhysicsMeshResult VoxelPhysicsMesh::generateFromVoxelData(
    const ChunkVoxelData* chunk,
    const ChunkCoord& chunkCoord,
    PhysicsTriangle* outTriangles)
{
    PhysicsMeshResult result;
    result.success = false;

    if (!chunk || !outTriangles) {
        return result;
    }

    uint32_t triangleIndex = 0;
    glm::vec3 worldOffset = chunkCoord.toWorldPos();

    // Initialize AABB
    result.aabb.min = glm::vec3(std::numeric_limits<float>::max());
    result.aabb.max = glm::vec3(std::numeric_limits<float>::lowest());

    // Generate faces for each solid voxel
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                BlockType block = chunk->getBlock(x, y, z);

                if (!isSolid(block)) {
                    continue;
                }

                // World position of voxel corner
                glm::vec3 pos = worldOffset + glm::vec3(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z)
                );

                // Update AABB
                result.aabb.min = glm::min(result.aabb.min, pos);
                result.aabb.max = glm::max(result.aabb.max, pos + glm::vec3(1.0f));

                // Check each face (6 directions)
                // +X face
                BlockType neighborPX = (x < CHUNK_SIZE - 1) ?
                    chunk->getBlock(x + 1, y, z) : BLOCK_AIR;
                if (shouldGenerateFace(block, neighborPX) && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK) {
                    addQuadTriangles(
                        pos + glm::vec3(1, 0, 0),
                        pos + glm::vec3(1, 1, 0),
                        pos + glm::vec3(1, 1, 1),
                        pos + glm::vec3(1, 0, 1),
                        outTriangles, triangleIndex);
                }

                // -X face
                BlockType neighborNX = (x > 0) ?
                    chunk->getBlock(x - 1, y, z) : BLOCK_AIR;
                if (shouldGenerateFace(block, neighborNX) && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK) {
                    addQuadTriangles(
                        pos + glm::vec3(0, 0, 1),
                        pos + glm::vec3(0, 1, 1),
                        pos + glm::vec3(0, 1, 0),
                        pos + glm::vec3(0, 0, 0),
                        outTriangles, triangleIndex);
                }

                // +Y face
                BlockType neighborPY = (y < CHUNK_SIZE - 1) ?
                    chunk->getBlock(x, y + 1, z) : BLOCK_AIR;
                if (shouldGenerateFace(block, neighborPY) && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK) {
                    addQuadTriangles(
                        pos + glm::vec3(0, 1, 0),
                        pos + glm::vec3(0, 1, 1),
                        pos + glm::vec3(1, 1, 1),
                        pos + glm::vec3(1, 1, 0),
                        outTriangles, triangleIndex);
                }

                // -Y face
                BlockType neighborNY = (y > 0) ?
                    chunk->getBlock(x, y - 1, z) : BLOCK_AIR;
                if (shouldGenerateFace(block, neighborNY) && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK) {
                    addQuadTriangles(
                        pos + glm::vec3(0, 0, 1),
                        pos + glm::vec3(0, 0, 0),
                        pos + glm::vec3(1, 0, 0),
                        pos + glm::vec3(1, 0, 1),
                        outTriangles, triangleIndex);
                }

                // +Z face
                BlockType neighborPZ = (z < CHUNK_SIZE - 1) ?
                    chunk->getBlock(x, y, z + 1) : BLOCK_AIR;
                if (shouldGenerateFace(block, neighborPZ) && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK) {
                    addQuadTriangles(
                        pos + glm::vec3(0, 0, 1),
                        pos + glm::vec3(1, 0, 1),
                        pos + glm::vec3(1, 1, 1),
                        pos + glm::vec3(0, 1, 1),
                        outTriangles, triangleIndex);
                }

                // -Z face
                BlockType neighborNZ = (z > 0) ?
                    chunk->getBlock(x, y, z - 1) : BLOCK_AIR;
                if (shouldGenerateFace(block, neighborNZ) && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK) {
                    addQuadTriangles(
                        pos + glm::vec3(1, 0, 0),
                        pos + glm::vec3(0, 0, 0),
                        pos + glm::vec3(0, 1, 0),
                        pos + glm::vec3(1, 1, 0),
                        outTriangles, triangleIndex);
                }
            }
        }
    }

    result.success = true;
    result.triangleCount = triangleIndex;
    return result;
}

PhysicsMeshResult VoxelPhysicsMesh::generateFromStbMesh(
    const void* stbVertices,
    uint32_t numVertices,
    const glm::vec3& scale,
    const glm::vec3& chunkWorldPos,
    PhysicsTriangle* outTriangles)
{
    PhysicsMeshResult result;
    result.success = false;

    if (!stbVertices || !outTriangles || numVertices == 0) {
        return result;
    }

    // stb_voxel_render outputs quads (4 vertices each)
    if (numVertices % 4 != 0) {
        return result;
    }

    struct StbVoxelVertex {
        uint32_t attr_vertex;
        uint32_t attr_face;
    };

    const StbVoxelVertex* verts = static_cast<const StbVoxelVertex*>(stbVertices);
    uint32_t numQuads = numVertices / 4;
    uint32_t triangleIndex = 0;

    // Initialize AABB
    result.aabb.min = glm::vec3(std::numeric_limits<float>::max());
    result.aabb.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (uint32_t q = 0; q < numQuads && triangleIndex + 2 <= MAX_TRIANGLES_PER_CHUNK; ++q) {
        const StbVoxelVertex* quad = &verts[q * 4];

        // Unpack positions
        glm::vec3 v0 = unpackStbPosition(quad[0].attr_vertex, scale) + chunkWorldPos;
        glm::vec3 v1 = unpackStbPosition(quad[1].attr_vertex, scale) + chunkWorldPos;
        glm::vec3 v2 = unpackStbPosition(quad[2].attr_vertex, scale) + chunkWorldPos;
        glm::vec3 v3 = unpackStbPosition(quad[3].attr_vertex, scale) + chunkWorldPos;

        // Update AABB
        result.aabb.min = glm::min(result.aabb.min, glm::min(glm::min(v0, v1), glm::min(v2, v3)));
        result.aabb.max = glm::max(result.aabb.max, glm::max(glm::max(v0, v1), glm::max(v2, v3)));

        // Add as two triangles
        addQuadTriangles(v0, v1, v2, v3, outTriangles, triangleIndex);
    }

    result.success = true;
    result.triangleCount = triangleIndex;
    return result;
}

uint32_t VoxelPhysicsMesh::generateBoxColliders(
    const ChunkVoxelData* chunk,
    const ChunkCoord& chunkCoord,
    PhysicsAABB* outBoxes,
    uint32_t maxBoxes)
{
    if (!chunk || !outBoxes || maxBoxes == 0) {
        return 0;
    }

    uint32_t boxIndex = 0;
    glm::vec3 worldOffset = chunkCoord.toWorldPos();

    // Simple approach: one box per solid voxel
    // Future optimization: merge adjacent voxels into larger boxes
    for (int x = 0; x < CHUNK_SIZE && boxIndex < maxBoxes; ++x) {
        for (int y = 0; y < CHUNK_SIZE && boxIndex < maxBoxes; ++y) {
            for (int z = 0; z < CHUNK_SIZE && boxIndex < maxBoxes; ++z) {
                BlockType block = chunk->getBlock(x, y, z);

                if (!isSolid(block)) {
                    continue;
                }

                glm::vec3 pos = worldOffset + glm::vec3(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z)
                );

                outBoxes[boxIndex].min = pos;
                outBoxes[boxIndex].max = pos + glm::vec3(1.0f);
                boxIndex++;
            }
        }
    }

    return boxIndex;
}

// ============================================================================
// PhysicsMeshPool
// ============================================================================

PhysicsMeshPool::~PhysicsMeshPool() {
    shutdown();
}

bool PhysicsMeshPool::initialize(uint32_t numBuffers) {
    if (initialized_ || numBuffers == 0) {
        return false;
    }

    buffers_.reserve(numBuffers);
    inUse_.resize(numBuffers, false);

    for (uint32_t i = 0; i < numBuffers; ++i) {
        auto* buffer = new PhysicsTriangle[VoxelPhysicsMesh::MAX_TRIANGLES_PER_CHUNK];
        if (!buffer) {
            shutdown();
            return false;
        }
        buffers_.push_back(buffer);
    }

    numBuffers_ = numBuffers;
    initialized_ = true;
    return true;
}

void PhysicsMeshPool::shutdown() {
    for (auto* buffer : buffers_) {
        delete[] buffer;
    }
    buffers_.clear();
    inUse_.clear();
    numBuffers_ = 0;
    initialized_ = false;
}

PhysicsTriangle* PhysicsMeshPool::acquireBuffer() {
    if (!initialized_) {
        return nullptr;
    }

    for (uint32_t i = 0; i < numBuffers_; ++i) {
        if (!inUse_[i]) {
            inUse_[i] = true;
            return buffers_[i];
        }
    }

    return nullptr;
}

void PhysicsMeshPool::releaseBuffer(PhysicsTriangle* buffer) {
    if (!initialized_ || !buffer) {
        return;
    }

    for (uint32_t i = 0; i < numBuffers_; ++i) {
        if (buffers_[i] == buffer) {
            inUse_[i] = false;
            return;
        }
    }
}

uint32_t PhysicsMeshPool::getAvailableCount() const {
    if (!initialized_) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < numBuffers_; ++i) {
        if (!inUse_[i]) {
            count++;
        }
    }
    return count;
}

} // namespace voxel
} // namespace jupiter
