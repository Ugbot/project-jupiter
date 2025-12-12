/**
 * @file voxel_world.cpp
 * @brief Implementation of VoxelWorld
 */

#include <voxel/voxel_world.h>
#include <cstdio>
#include <cmath>
#include <chrono>

namespace jupiter {
namespace voxel {

VoxelWorld::~VoxelWorld() {
    if (initialized_) {
        shutdown();
    }
}

bool VoxelWorld::initialize(const VoxelWorldConfig& config) {
    if (initialized_) return false;

    printf("VoxelWorld::initialize: starting\n");
    fflush(stdout);

    config_ = config;

    printf("VoxelWorld::initialize: allocating subsystems\n");
    fflush(stdout);

    // Allocate all subsystems on heap
    chunkPool_ = std::make_unique<ChunkPool>();
    chunkMap_ = std::make_unique<ChunkMap>();
    dirtyQueue_ = std::make_unique<DirtyChunkQueue>();
    streamingManager_ = std::make_unique<StreamingManager>();
    meshingBudgeter_ = std::make_unique<MeshingBudgeter>();
    meshBufferPool_ = std::make_unique<MeshBufferPool>();
    mesher_ = std::make_unique<VoxelMesher>();

    printf("VoxelWorld::initialize: initializing chunkPool\n");
    fflush(stdout);

    // Initialize subsystems
    chunkPool_->initialize();

    printf("VoxelWorld::initialize: initializing chunkMap\n");
    fflush(stdout);

    chunkMap_->initialize();

    printf("VoxelWorld::initialize: initializing dirtyQueue\n");
    fflush(stdout);

    dirtyQueue_->initialize();

    printf("VoxelWorld::initialize: initializing streamingManager\n");
    fflush(stdout);

    streamingManager_->initialize(config.viewDistance);

    printf("VoxelWorld::initialize: initializing mesher\n");
    fflush(stdout);

    mesher_->initialize();

    printf("VoxelWorld::initialize: mesher done, initializing meshBufferPool\n");
    fflush(stdout);

    // Initialize mesh buffer pool
    if (!meshBufferPool_->initialize(MeshBufferPool::DEFAULT_BUFFER_COUNT)) {
        shutdown();
        return false;
    }

    printf("VoxelWorld::initialize: meshBufferPool done, configuring budgeter\n");
    fflush(stdout);

    // Configure budgeter
    meshingBudgeter_->setTargetFrameTime(config.targetFrameMs);
    meshingBudgeter_->setMeshingBudget(config.meshingBudgetPercent);

    printf("VoxelWorld::initialize: allocating convertedVertices\n");
    fflush(stdout);

    // Pre-allocate converted vertices buffer
    convertedVertices_ = std::make_unique<VoxelVertex[]>(MAX_CONVERTED_VERTICES);

    printf("VoxelWorld::initialize: done\n");
    fflush(stdout);

    initialized_ = true;
    return true;
}

void VoxelWorld::shutdown() {
    if (!initialized_) return;

    // Shutdown in reverse order
    mesher_->shutdown();
    meshBufferPool_->shutdown();
    streamingManager_->initialize(0);  // Clears state
    dirtyQueue_->initialize();
    chunkMap_->shutdown();
    chunkPool_->shutdown();

    // Release all heap allocations
    convertedVertices_.reset();
    mesher_.reset();
    meshBufferPool_.reset();
    meshingBudgeter_.reset();
    streamingManager_.reset();
    dirtyQueue_.reset();
    chunkMap_.reset();
    chunkPool_.reset();

    meshCallback_ = nullptr;
    unloadCallback_ = nullptr;

    initialized_ = false;
}

void VoxelWorld::update(const glm::vec3& cameraPos, float deltaTime) {
    if (!initialized_) return;

    chunksMeshedThisFrame_ = 0;
    meshingBudgeter_->beginFrame();

    // Update streaming manager
    auto isLoaded = [this](const ChunkCoord& coord) {
        return isChunkLoaded(coord);
    };
    streamingManager_->update(cameraPos, isLoaded);

    // Process load requests
    ChunkLoadRequest loadReq;
    while (streamingManager_->popLoadRequest(loadReq) && meshingBudgeter_->canMeshAnother()) {
        // Load chunk
        uint32_t poolIndex = loadChunk(loadReq.coord);
        if (poolIndex != ChunkPool::INVALID_INDEX) {
            // Mesh the newly loaded chunk
            auto start = std::chrono::high_resolution_clock::now();
            meshChunk(loadReq.coord, poolIndex);
            auto end = std::chrono::high_resolution_clock::now();

            float meshTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
            meshingBudgeter_->recordMeshing(meshTimeMs);
            chunksMeshedThisFrame_++;
        }
    }

    // Process dirty chunks (re-mesh edited chunks)
    ChunkCoord dirtyCoord;
    while (dirtyQueue_->pop(dirtyCoord) && meshingBudgeter_->canMeshAnother()) {
        uint32_t poolIndex = getChunkIndex(dirtyCoord);
        if (poolIndex != ChunkMap::INVALID_INDEX) {
            auto start = std::chrono::high_resolution_clock::now();
            meshChunk(dirtyCoord, poolIndex);
            auto end = std::chrono::high_resolution_clock::now();

            float meshTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
            meshingBudgeter_->recordMeshing(meshTimeMs);

            dirtyQueue_->clearDirtyBit(poolIndex);
            chunksMeshedThisFrame_++;
        }
    }

    // Process unload requests
    ChunkCoord unloadCoord;
    while (streamingManager_->popUnloadRequest(unloadCoord)) {
        unloadChunk(unloadCoord);
    }
}

BlockType VoxelWorld::getBlock(const glm::ivec3& worldPos) const {
    if (!initialized_) return BLOCK_AIR;

    ChunkCoord chunkCoord = worldToChunk(glm::vec3(worldPos));
    uint32_t poolIndex = getChunkIndex(chunkCoord);

    if (poolIndex == ChunkMap::INVALID_INDEX) {
        return BLOCK_AIR;  // Chunk not loaded
    }

    glm::ivec3 local = worldToLocal(glm::vec3(worldPos));
    const ChunkVoxelData& chunk = chunkPool_->at(poolIndex);

    return chunk.getBlock(local.x, local.y, local.z);
}

bool VoxelWorld::setBlock(const glm::ivec3& worldPos, BlockType type) {
    if (!initialized_) return false;

    ChunkCoord chunkCoord = worldToChunk(glm::vec3(worldPos));
    uint32_t poolIndex = getChunkIndex(chunkCoord);

    if (poolIndex == ChunkMap::INVALID_INDEX) {
        return false;  // Chunk not loaded
    }

    glm::ivec3 local = worldToLocal(glm::vec3(worldPos));
    ChunkVoxelData& chunk = chunkPool_->at(poolIndex);

    chunk.setBlock(local.x, local.y, local.z, type);

    // Mark chunk and neighbors as dirty
    auto getPoolIdx = [this](const ChunkCoord& c) {
        return getChunkIndex(c);
    };
    markDirtyWithNeighbors(*dirtyQueue_, chunkCoord, poolIndex,
                          local.x, local.y, local.z, getPoolIdx);

    return true;
}

bool VoxelWorld::isSolid(const glm::ivec3& worldPos) const {
    return getBlock(worldPos) != BLOCK_AIR;
}

VoxelRaycastResult VoxelWorld::raycast(const glm::vec3& origin,
                                       const glm::vec3& direction,
                                       float maxDistance) const
{
    VoxelRaycastResult result;

    if (!initialized_) return result;

    // DDA algorithm for voxel traversal
    glm::vec3 pos = origin;
    glm::ivec3 mapPos = glm::ivec3(std::floor(pos.x), std::floor(pos.y), std::floor(pos.z));

    glm::vec3 deltaDist = glm::abs(glm::vec3(1.0f) / direction);
    glm::ivec3 step;
    glm::vec3 sideDist;

    // Calculate step and initial sideDist
    for (int i = 0; i < 3; ++i) {
        if (direction[i] < 0) {
            step[i] = -1;
            sideDist[i] = (pos[i] - mapPos[i]) * deltaDist[i];
        } else {
            step[i] = 1;
            sideDist[i] = (mapPos[i] + 1.0f - pos[i]) * deltaDist[i];
        }
    }

    float distance = 0.0f;
    int side = 0;

    while (distance < maxDistance) {
        // Check current voxel
        BlockType block = getBlock(mapPos);
        if (block != BLOCK_AIR) {
            result.hit = true;
            result.blockPos = mapPos;
            result.blockType = block;
            result.distance = distance;
            result.chunkCoord = worldToChunk(glm::vec3(mapPos));

            // Calculate normal based on which side was hit
            result.blockNormal = glm::ivec3(0);
            result.blockNormal[side] = -step[side];

            return result;
        }

        // Move to next voxel
        if (sideDist.x < sideDist.y) {
            if (sideDist.x < sideDist.z) {
                distance = sideDist.x;
                sideDist.x += deltaDist.x;
                mapPos.x += step.x;
                side = 0;
            } else {
                distance = sideDist.z;
                sideDist.z += deltaDist.z;
                mapPos.z += step.z;
                side = 2;
            }
        } else {
            if (sideDist.y < sideDist.z) {
                distance = sideDist.y;
                sideDist.y += deltaDist.y;
                mapPos.y += step.y;
                side = 1;
            } else {
                distance = sideDist.z;
                sideDist.z += deltaDist.z;
                mapPos.z += step.z;
                side = 2;
            }
        }
    }

    return result;
}

bool VoxelWorld::isChunkLoaded(const ChunkCoord& coord) const {
    return chunkMap_->contains(coord);
}

uint32_t VoxelWorld::getChunkIndex(const ChunkCoord& coord) const {
    return chunkMap_->find(coord);
}

const ChunkVoxelData* VoxelWorld::getChunkData(const ChunkCoord& coord) const {
    uint32_t index = getChunkIndex(coord);
    if (index == ChunkMap::INVALID_INDEX) {
        return nullptr;
    }
    return &chunkPool_->at(index);
}

uint32_t VoxelWorld::loadChunk(const ChunkCoord& coord) {
    if (!initialized_) return ChunkPool::INVALID_INDEX;

    // Check if already loaded
    if (isChunkLoaded(coord)) {
        return getChunkIndex(coord);
    }

    // Allocate from pool
    uint32_t poolIndex = chunkPool_->allocate();
    if (poolIndex == ChunkPool::INVALID_INDEX) {
        return ChunkPool::INVALID_INDEX;  // Pool exhausted
    }

    // Insert into map
    if (!chunkMap_->insert(coord, poolIndex)) {
        chunkPool_->release(poolIndex);
        return ChunkPool::INVALID_INDEX;  // Map full
    }

    // Generate terrain
    ChunkVoxelData& chunk = chunkPool_->at(poolIndex);
    generateChunkTerrain(&chunk, coord);

    return poolIndex;
}

void VoxelWorld::unloadChunk(const ChunkCoord& coord) {
    if (!initialized_) return;

    uint32_t poolIndex = getChunkIndex(coord);
    if (poolIndex == ChunkMap::INVALID_INDEX) {
        return;  // Not loaded
    }

    // Notify callback
    if (unloadCallback_) {
        unloadCallback_(coord, poolIndex);
    }

    // Remove from map and release
    chunkMap_->remove(coord);
    chunkPool_->release(poolIndex);
}

uint32_t VoxelWorld::getLoadedChunkCount() const {
    return chunkPool_ ? chunkPool_->getAllocatedCount() : 0;
}

uint32_t VoxelWorld::getPendingMeshCount() const {
    return streamingManager_ ? static_cast<uint32_t>(streamingManager_->getPendingLoadCount()) : 0;
}

uint32_t VoxelWorld::getChunksMeshedThisFrame() const {
    return chunksMeshedThisFrame_;
}

void VoxelWorld::generateChunkTerrain(ChunkVoxelData* chunk, const ChunkCoord& coord) {
    generateProceduralTerrain(chunk, coord, config_.seed);
}

void VoxelWorld::getNeighborChunks(const ChunkCoord& coord,
                                   const ChunkVoxelData* neighbors[6]) const
{
    // +X, -X, +Y, -Y, +Z, -Z
    static const ChunkCoord offsets[6] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };

    for (int i = 0; i < 6; ++i) {
        ChunkCoord neighborCoord{
            coord.x + offsets[i].x,
            coord.y + offsets[i].y,
            coord.z + offsets[i].z
        };
        neighbors[i] = getChunkData(neighborCoord);
    }
}

void VoxelWorld::meshChunk(const ChunkCoord& coord, uint32_t poolIndex) {
    const ChunkVoxelData& chunk = chunkPool_->at(poolIndex);

    // Get neighbor chunks for proper face culling
    const ChunkVoxelData* neighbors[6];
    getNeighborChunks(coord, neighbors);

    // Acquire buffer from pool
    MeshBuffer* meshBuffer = meshBufferPool_->acquire();
    if (!meshBuffer) {
        return;  // No buffers available
    }

    // Set buffer on mesher
    mesher_->setBuffer(meshBuffer);

    // Begin meshing
    mesher_->beginChunk(&chunk, neighbors, coord);

    // Meshify (may need multiple calls if buffer fills)
    MeshResult result;
    uint32_t totalVertices = 0;

    do {
        result = mesher_->meshify();

        if (result.numVertices > 0 && meshCallback_) {
            // Convert stb vertices to our format
            const StbVoxelVertex* stbVerts = mesher_->getStbVertexBuffer();

            // Ensure we don't overflow the pre-allocated buffer
            uint32_t vertsToCopy = result.numVertices;
            if (totalVertices + vertsToCopy > MAX_CONVERTED_VERTICES) {
                vertsToCopy = MAX_CONVERTED_VERTICES - totalVertices;
            }

            VoxelMesher::convertMesh(stbVerts, vertsToCopy,
                                    convertedVertices_.get() + totalVertices,
                                    coord, result.scale);
            totalVertices += vertsToCopy;
        }
    } while (!result.volumeDone && totalVertices < MAX_CONVERTED_VERTICES);

    // Release buffer back to pool
    meshBufferPool_->release(meshBuffer);

    // Notify callback with full mesh
    if (totalVertices > 0 && meshCallback_) {
        meshCallback_(coord, poolIndex,
                     convertedVertices_.get(),
                     totalVertices,
                     result.aabbMin, result.aabbMax);
    }
}

} // namespace voxel
} // namespace jupiter
