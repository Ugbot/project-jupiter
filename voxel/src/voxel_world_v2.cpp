/**
 * @file voxel_world_v2.cpp
 * @brief Implementation of VoxelWorldV2
 */

#include <voxel/voxel_world_v2.h>
#include <voxel/block_registry.h>
#include <voxel/terrain_rules.h>
#include <cmath>
#include <chrono>

namespace jupiter {
namespace voxel {

VoxelWorldV2::~VoxelWorldV2() {
    if (initialized_) {
        shutdown();
    }
}

bool VoxelWorldV2::initialize(const VoxelWorldV2Config& config) {
    if (initialized_) return false;
    
    config_ = config;
    
    // Allocate subsystems
    chunkPool_ = std::make_unique<ChunkPool>();
    chunkMap_ = std::make_unique<ChunkMap>();
    dirtyQueue_ = std::make_unique<DirtyChunkQueue>();
    streamingManager_ = std::make_unique<StreamingManager>();
    meshingBudgeter_ = std::make_unique<MeshingBudgeter>();
    
    // Initialize subsystems
    chunkPool_->initialize();
    chunkMap_->initialize();
    dirtyQueue_->initialize();
    streamingManager_->initialize(config.viewDistance);
    
    // Configure budgeter
    meshingBudgeter_->setTargetFrameTime(config.targetFrameMs);
    meshingBudgeter_->setMeshingBudget(config.meshingBudgetPercent);
    
    // Pre-allocate columnar storage pool (all memory allocated here, no per-chunk allocations)
    chunkColumnsPool_.initialize(config.maxChunks);
    chunkColumnsMap_.resize(config.maxChunks, nullptr);
    
    // Pre-allocate density storage for smooth mode
    if (usesDensity(config.meshMode)) {
        densityStorage_.resize(config.maxChunks);
        for (size_t i = 0; i < config.maxChunks; ++i) {
            densityStorage_[i] = std::make_unique<ChunkDensity>();
        }
    }
    
    // Register builtin kernels
    kernels::registerBuiltinKernels();
    
    // Load block definitions
    BlockRegistry::instance().registerDefaults();
    if (!config.blocksJsonPath.empty()) {
        BlockRegistry::instance().loadFromJSON(config.blocksJsonPath);
    }
    
    // Load terrain rules
    TerrainRules::instance().registerDefaults();
    if (!config.terrainJsonPath.empty()) {
        TerrainRules::instance().loadFromJSON(config.terrainJsonPath);
    }
    
    // Configure smooth mesh kernel
    smoothMeshKernel_.setDefaultConfig(config.meshConfig);
    
    initialized_ = true;
    return true;
}

void VoxelWorldV2::shutdown() {
    if (!initialized_) return;
    
    // Clear columnar storage (pool handles deallocation)
    chunkColumnsMap_.clear();
    densityStorage_.clear();
    // Note: chunkColumnsPool_ destructor handles memory
    
    // Clear CSG pool
    csgPool_.clear();
    
    // Shutdown subsystems
    streamingManager_->initialize(0);
    dirtyQueue_->initialize();
    chunkMap_->shutdown();
    chunkPool_->shutdown();
    
    // Release allocations
    meshingBudgeter_.reset();
    streamingManager_.reset();
    dirtyQueue_.reset();
    chunkMap_.reset();
    chunkPool_.reset();
    
    // Clear callbacks
    meshCallback_ = nullptr;
    rawMeshCallback_ = nullptr;
    kernelMeshBufferCallback_ = nullptr;
    unloadCallback_ = nullptr;
    smoothMeshCallback_ = nullptr;
    
    // Clear LOD tracking
    chunkLODs_.clear();
    
    initialized_ = false;
}

void VoxelWorldV2::update(const glm::vec3& cameraPos, float deltaTime) {
    if (!initialized_) return;
    
    chunksMeshedThisFrame_ = 0;
    commandsProcessedThisFrame_ = 0;
    meshingBudgeter_->beginFrame();
    
    // Process command queue first
    processCommands();
    
    // Update streaming manager
    auto isLoaded = [this](const ChunkCoord& coord) {
        return isChunkLoaded(coord);
    };
    bool cameraMovedChunk = streamingManager_->update(cameraPos, isLoaded);
    
    // Handle chunk unloading when camera moves
    if (cameraMovedChunk) {
        const int viewDist = streamingManager_->getViewDistance();
        const int unloadDist = viewDist + 2;
        
        chunkMap_->forEach([&](const ChunkCoord& coord, uint32_t poolIndex) {
            if (!streamingManager_->isInViewDistance(coord, cameraPos)) {
                ChunkCoord cameraChunk = worldToChunk(cameraPos);
                int dx = coord.x - cameraChunk.x;
                int dy = coord.y - cameraChunk.y;
                int dz = coord.z - cameraChunk.z;
                float dist = std::sqrt(static_cast<float>(dx*dx + dy*dy + dz*dz));
                
                if (dist > static_cast<float>(unloadDist)) {
                    streamingManager_->requestUnload(coord);
                }
            }
            return true;
        });
    }
    
    // Process load requests
    ChunkLoadRequest loadReq;
    while (streamingManager_->popLoadRequest(loadReq) && meshingBudgeter_->canMeshAnother()) {
        uint32_t poolIndex = loadChunk(loadReq.coord);
        if (poolIndex != ChunkPool::INVALID_INDEX) {
            auto start = std::chrono::high_resolution_clock::now();
            
            if (config_.useKernelMeshing) {
                if (usesDensity(config_.meshMode)) {
                    // Generate density and mesh with smooth kernel
                    meshChunkSmooth(loadReq.coord);
                } else {
                    // Use blocky kernel meshing
                    meshChunkKernel(loadReq.coord, poolIndex);
                }
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            float meshTimeMs = std::chrono::duration<float, std::milli>(end - start).count();
            meshingBudgeter_->recordMeshing(meshTimeMs);
            chunksMeshedThisFrame_++;
        }
    }
    
    // Process dirty chunks
    ChunkCoord dirtyCoord;
    while (dirtyQueue_->pop(dirtyCoord) && meshingBudgeter_->canMeshAnother()) {
        uint32_t poolIndex = getChunkIndex(dirtyCoord);
        if (poolIndex != ChunkMap::INVALID_INDEX) {
            auto start = std::chrono::high_resolution_clock::now();
            
            if (config_.useKernelMeshing) {
                if (usesDensity(config_.meshMode)) {
                    meshChunkSmooth(dirtyCoord);
                } else {
                    meshChunkKernel(dirtyCoord, poolIndex);
                }
            }
            
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
        // Remove LOD tracking when chunk unloads
        chunkLODs_.erase(unloadCoord);
        unloadChunk(unloadCoord);
    }
}

void VoxelWorldV2::processCommands() {
    std::vector<VoxelCommand> commands;
    commandQueue_.drainAll(commands, true);  // Sort by priority
    
    commandsProcessedThisFrame_ = static_cast<uint32_t>(commands.size());
    
    for (const auto& cmd : commands) {
        applyCommand(cmd);
    }
}

void VoxelWorldV2::applyCommand(const VoxelCommand& cmd) {
    switch (cmd.type) {
        case VoxelCommandType::SetBlock: {
            glm::ivec3 pos(cmd.data.setBlock.x, cmd.data.setBlock.y, cmd.data.setBlock.z);
            setBlock(pos, cmd.data.setBlock.block);
            break;
        }
        
        case VoxelCommandType::FillBox: {
            for (int32_t z = cmd.data.fillBox.minZ; z <= cmd.data.fillBox.maxZ; ++z) {
                for (int32_t y = cmd.data.fillBox.minY; y <= cmd.data.fillBox.maxY; ++y) {
                    for (int32_t x = cmd.data.fillBox.minX; x <= cmd.data.fillBox.maxX; ++x) {
                        setBlock(glm::ivec3(x, y, z), cmd.data.fillBox.block);
                    }
                }
            }
            break;
        }
        
        case VoxelCommandType::CSGUnion:
        case VoxelCommandType::CSGDifference:
        case VoxelCommandType::CSGIntersection:
        case VoxelCommandType::CSGReplace: {
            const CSGPrimitive* prim = csgPool_.get(cmd.data.csg.primitiveIndex);
            if (!prim) break;
            
            // Find affected chunks
            glm::vec3 primMin = prim->position - prim->scale;
            glm::vec3 primMax = prim->position + prim->scale;
            
            ChunkCoord minChunk = worldToChunk(primMin);
            ChunkCoord maxChunk = worldToChunk(primMax);
            
            for (int cz = minChunk.z; cz <= maxChunk.z; ++cz) {
                for (int cy = minChunk.y; cy <= maxChunk.y; ++cy) {
                    for (int cx = minChunk.x; cx <= maxChunk.x; ++cx) {
                        ChunkCoord coord{cx, cy, cz};
                        uint32_t poolIndex = getChunkIndex(coord);
                        
                        if (poolIndex == ChunkMap::INVALID_INDEX) continue;
                        
                        if (config_.useColumnarStorage && chunkColumnsMap_[poolIndex]) {
                            // Apply CSG directly to columnar storage (no copy needed)
                            csgEvaluator_.evaluate(*prim, *chunkColumnsMap_[poolIndex], coord);
                        } else {
                            ChunkVoxelData& chunk = chunkPool_->at(poolIndex);
                            csgEvaluator_.evaluate(*prim, chunk.blocks, coord);
                        }
                        
                        // Mark dirty
                        dirtyQueue_->markDirty(coord, poolIndex);
                    }
                }
            }
            break;
        }
        
        case VoxelCommandType::InvalidateChunk: {
            ChunkCoord coord = cmd.data.chunkOp.toCoord();
            uint32_t poolIndex = getChunkIndex(coord);
            if (poolIndex != ChunkMap::INVALID_INDEX) {
                dirtyQueue_->markDirty(coord, poolIndex);
            }
            break;
        }
        
        case VoxelCommandType::UnloadChunk: {
            unloadChunk(cmd.data.chunkOp.toCoord());
            break;
        }
        
        default:
            break;
    }
}

BlockType VoxelWorldV2::getBlock(const glm::ivec3& worldPos) const {
    if (!initialized_) return BLOCK_AIR;
    
    ChunkCoord chunkCoord = worldToChunk(glm::vec3(worldPos));
    uint32_t poolIndex = getChunkIndex(chunkCoord);
    
    if (poolIndex == ChunkMap::INVALID_INDEX) {
        return BLOCK_AIR;
    }
    
    glm::ivec3 local = worldToLocal(glm::vec3(worldPos));
    
    if (config_.useColumnarStorage && chunkColumnsMap_[poolIndex]) {
        return chunkColumnsMap_[poolIndex]->getBlock(local.x, local.y, local.z);
    }
    
    const ChunkVoxelData& chunk = chunkPool_->at(poolIndex);
    return chunk.getBlock(local.x, local.y, local.z);
}

bool VoxelWorldV2::setBlock(const glm::ivec3& worldPos, BlockType type) {
    if (!initialized_) return false;
    
    ChunkCoord chunkCoord = worldToChunk(glm::vec3(worldPos));
    uint32_t poolIndex = getChunkIndex(chunkCoord);
    
    if (poolIndex == ChunkMap::INVALID_INDEX) {
        return false;
    }
    
    glm::ivec3 local = worldToLocal(glm::vec3(worldPos));
    
    if (config_.useColumnarStorage && chunkColumnsMap_[poolIndex]) {
        chunkColumnsMap_[poolIndex]->setBlock(local.x, local.y, local.z, type);
        chunkColumnsMap_[poolIndex]->at(local.x, local.z).updateBounds();
        
        // Sync back to flat storage
        ChunkVoxelData& chunk = chunkPool_->at(poolIndex);
        chunk.setBlock(local.x, local.y, local.z, type);
    } else {
        ChunkVoxelData& chunk = chunkPool_->at(poolIndex);
        chunk.setBlock(local.x, local.y, local.z, type);
    }
    
    // Mark dirty with neighbors
    auto getPoolIdx = [this](const ChunkCoord& c) {
        return getChunkIndex(c);
    };
    markDirtyWithNeighbors(*dirtyQueue_, chunkCoord, poolIndex,
                          local.x, local.y, local.z, getPoolIdx);
    
    return true;
}

bool VoxelWorldV2::isSolid(const glm::ivec3& worldPos) const {
    return getBlock(worldPos) != BLOCK_AIR;
}

VoxelRaycastResult VoxelWorldV2::raycast(const glm::vec3& origin,
                                         const glm::vec3& direction,
                                         float maxDistance) const {
    VoxelRaycastResult result;
    
    if (!initialized_) return result;
    
    // DDA algorithm
    glm::vec3 pos = origin;
    glm::ivec3 mapPos = glm::ivec3(std::floor(pos.x), std::floor(pos.y), std::floor(pos.z));
    
    glm::vec3 deltaDist = glm::abs(glm::vec3(1.0f) / direction);
    glm::ivec3 step;
    glm::vec3 sideDist;
    
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
        BlockType block = getBlock(mapPos);
        if (block != BLOCK_AIR) {
            result.hit = true;
            result.blockPos = mapPos;
            result.blockType = block;
            result.distance = distance;
            result.chunkCoord = worldToChunk(glm::vec3(mapPos));
            result.blockNormal = glm::ivec3(0);
            result.blockNormal[side] = -step[side];
            return result;
        }
        
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

bool VoxelWorldV2::isChunkLoaded(const ChunkCoord& coord) const {
    return chunkMap_->contains(coord);
}

uint32_t VoxelWorldV2::getChunkIndex(const ChunkCoord& coord) const {
    return chunkMap_->find(coord);
}

const ChunkVoxelData* VoxelWorldV2::getChunkData(const ChunkCoord& coord) const {
    uint32_t index = getChunkIndex(coord);
    if (index == ChunkMap::INVALID_INDEX) {
        return nullptr;
    }
    return &chunkPool_->at(index);
}

const ChunkColumns* VoxelWorldV2::getChunkColumns(const ChunkCoord& coord) const {
    uint32_t index = getChunkIndex(coord);
    if (index == ChunkMap::INVALID_INDEX) {
        return nullptr;
    }
    if (index >= chunkColumnsMap_.size()) {
        return nullptr;
    }
    return chunkColumnsMap_[index];
}

uint32_t VoxelWorldV2::loadChunk(const ChunkCoord& coord) {
    if (!initialized_) return ChunkPool::INVALID_INDEX;
    
    if (isChunkLoaded(coord)) {
        return getChunkIndex(coord);
    }
    
    uint32_t poolIndex = chunkPool_->allocate();
    if (poolIndex == ChunkPool::INVALID_INDEX) {
        return ChunkPool::INVALID_INDEX;
    }
    
    if (!chunkMap_->insert(coord, poolIndex)) {
        chunkPool_->release(poolIndex);
        return ChunkPool::INVALID_INDEX;
    }
    
    // Create columnar storage if enabled (uses pre-allocated pool)
    if (config_.useColumnarStorage) {
        // Ensure map is big enough
        if (poolIndex >= chunkColumnsMap_.size()) {
            chunkColumnsMap_.resize(poolIndex + 1, nullptr);
        }
        
        // Acquire from pool (O(1), no allocation)
        ChunkColumns* chunk = chunkColumnsPool_.acquire();
        if (!chunk) {
            // Pool exhausted - this shouldn't happen if maxChunks is configured correctly
            chunkMap_->remove(coord);
            chunkPool_->release(poolIndex);
            return ChunkMap::INVALID_INDEX;
        }
        chunkColumnsMap_[poolIndex] = chunk;
        
        // Attach density storage if in smooth mode
        if (usesDensity(config_.meshMode) && poolIndex < densityStorage_.size()) {
            chunk->setDensityStorage(densityStorage_[poolIndex].get());
            densityStorage_[poolIndex]->clear();
        }
        
        // Generate terrain using kernel
        generateChunkTerrainKernel(chunk, coord);
        
        // NOTE: No toFlat() call - meshing reads directly from ChunkColumns
    } else {
        // Legacy terrain generation
        ChunkVoxelData& chunk = chunkPool_->at(poolIndex);
        generateProceduralTerrain(&chunk, coord, config_.seed);
    }
    
    return poolIndex;
}

void VoxelWorldV2::unloadChunk(const ChunkCoord& coord) {
    if (!initialized_) return;
    
    uint32_t poolIndex = getChunkIndex(coord);
    if (poolIndex == ChunkMap::INVALID_INDEX) {
        return;
    }
    
    if (unloadCallback_) {
        unloadCallback_(coord, poolIndex);
    }
    
    // Release columnar storage back to pool (O(1), no deallocation)
    if (config_.useColumnarStorage && poolIndex < chunkColumnsMap_.size()) {
        ChunkColumns* chunk = chunkColumnsMap_[poolIndex];
        if (chunk) {
            // Detach density storage
            chunk->setDensityStorage(nullptr);
            // Return to pool
            chunkColumnsPool_.release(chunk);
            chunkColumnsMap_[poolIndex] = nullptr;
        }
    }
    
    chunkMap_->remove(coord);
    chunkPool_->release(poolIndex);
}

uint32_t VoxelWorldV2::getLoadedChunkCount() const {
    return chunkPool_ ? chunkPool_->getAllocatedCount() : 0;
}

uint32_t VoxelWorldV2::getPendingMeshCount() const {
    return streamingManager_ ? static_cast<uint32_t>(streamingManager_->getPendingLoadCount()) : 0;
}

uint32_t VoxelWorldV2::getChunksMeshedThisFrame() const {
    return chunksMeshedThisFrame_;
}

void VoxelWorldV2::generateChunkTerrainKernel(ChunkColumns* chunk, const ChunkCoord& coord) {
    if (!chunk) return;
    
    // Use custom terrain generator if set
    if (terrainGenerator_) {
        terrainGenerator_(*chunk, coord);
        chunk->incrementGeneration();
        return;
    }
    
    // Fallback to kernel-based generation
    VoxelExecBatch batch;
    batch.chunkCoord = coord;
    batch.voxelCount = CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT;
    batch.setColumn(VoxelColumnId::Blocks, chunk);
    
    VoxelKernelContext ctx;
    ctx.seed = config_.seed;
    ctx.world = nullptr;  // We don't have a VoxelWorld* here
    
    auto& registry = VoxelKernelRegistry::instance();
    registry.execute("terrain_noise", batch, batch, ctx);
}

void VoxelWorldV2::meshChunkKernel(const ChunkCoord& coord, uint32_t poolIndex) {
    if (!config_.useColumnarStorage) return;
    if (poolIndex >= chunkColumnsMap_.size() || !chunkColumnsMap_[poolIndex]) {
        return;
    }
    
    ChunkColumns* chunk = chunkColumnsMap_[poolIndex];
    
    // Get neighbor columns
    const ChunkColumns* neighbors[6];
    getNeighborColumns(coord, neighbors);
    
    // Create input batch
    VoxelExecBatch input;
    input.chunkCoord = coord;
    input.voxelCount = CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT;
    input.setColumn(VoxelColumnId::Blocks, const_cast<ChunkColumns*>(chunk));
    
    // Create output batch with mesh buffer
    KernelMeshBuffer* meshBuf = kernelMeshPool_.acquire();
    if (!meshBuf) return;
    
    VoxelExecBatch output;
    output.chunkCoord = coord;
    output.setColumn(VoxelColumnId::MeshBuffer, meshBuf);
    
    // Create context with neighbors
    VoxelKernelContext ctx;
    ctx.deltaTime = 0.0f;
    ctx.seed = config_.seed;
    for (int i = 0; i < 6; ++i) {
        ctx.neighborChunks[i] = neighbors[i];
    }
    
    // Execute mesh kernel
    auto& registry = VoxelKernelRegistry::instance();
    VoxelStatus status = registry.execute("mesh_chunk", input, output, ctx);
    
    if (isOk(status) && !meshBuf->vertices.empty()) {
        // Notify callback
        if (kernelMeshBufferCallback_) {
            kernelMeshBufferCallback_(coord, poolIndex, *meshBuf);
        }
        
        // Generate raw vertices for legacy callback
        if (rawMeshCallback_) {
            rawMeshCallback_(coord, poolIndex,
                            meshBuf->vertices.data(),
                            static_cast<uint32_t>(meshBuf->vertices.size()),
                            glm::vec3(1.0f),
                            glm::vec3(0.0f),
                            glm::vec3(CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE));
        }
    }
    
    kernelMeshPool_.release(meshBuf);
}

void VoxelWorldV2::getNeighborColumns(const ChunkCoord& coord,
                                       const ChunkColumns* neighbors[6]) const {
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
        neighbors[i] = getChunkColumns(neighborCoord);
    }
}

void VoxelWorldV2::generateChunkDensity(ChunkColumns* chunk, const ChunkCoord& coord) {
    if (!chunk || !chunk->hasDensity()) return;
    
    // Get biome for this chunk
    float worldX = static_cast<float>(coord.x * CHUNK_SIZE);
    float worldZ = static_cast<float>(coord.z * CHUNK_SIZE);
    uint8_t biomeId = TerrainRules::instance().getBiomeAt(worldX, worldZ, config_.seed);
    const BiomeDefinition& biome = TerrainRules::instance().getBiome(biomeId);
    
    // Simple hash-based noise for surface height variation (fast)
    auto noise2D = [this](float x, float z) -> float {
        int ix = static_cast<int>(x * 0.1f);
        int iz = static_cast<int>(z * 0.1f);
        uint32_t h = config_.seed;
        h ^= static_cast<uint32_t>(ix) * 374761393u;
        h ^= static_cast<uint32_t>(iz) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>(h & 0xFFFF) / 65535.0f;
    };
    
    // Generate density for each column
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            // Get world position
            float wx = worldX + x;
            float wz = worldZ + z;
            
            // Calculate surface height
            float noiseVal = noise2D(wx, wz);
            float surfaceHeight = biome.baseHeight + noiseVal * biome.heightVariation;
            
            // Generate density for each Y level
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                float wy = static_cast<float>(coord.y * CHUNK_SIZE + y);
                
                // SDF: negative = inside, positive = outside
                float density = wy - surfaceHeight;
                
                // Use ChunkColumns::setDensity (accesses external density storage)
                chunk->setDensity(x, y, z, density);
            }
        }
    }
}

void VoxelWorldV2::meshChunkSmooth(const ChunkCoord& coord) {
    uint32_t poolIndex = getChunkIndex(coord);
    if (poolIndex == ChunkMap::INVALID_INDEX) return;
    
    if (!config_.useColumnarStorage || poolIndex >= chunkColumnsMap_.size() ||
        !chunkColumnsMap_[poolIndex]) {
        return;
    }
    
    ChunkColumns* chunk = chunkColumnsMap_[poolIndex];
    
    // Generate density if needed (for smooth meshing)
    generateChunkDensity(chunk, coord);
    
    // Get neighbor columns
    const ChunkColumns* neighbors[6];
    getNeighborColumns(coord, neighbors);
    
    // Get LOD levels for this chunk and neighbors
    LODLevel chunkLOD = getChunkLOD(coord);
    LODLevel neighborLODs[6];
    const FaceDirection neighborDirs[6] = {
        FACE_POS_X, FACE_NEG_X,
        FACE_POS_Y, FACE_NEG_Y,
        FACE_POS_Z, FACE_NEG_Z
    };
    for (int i = 0; i < 6; ++i) {
        if (neighbors[i]) {
            // Calculate neighbor coord
            ChunkCoord neighborCoord = coord;
            switch (neighborDirs[i]) {
                case FACE_POS_X: neighborCoord.x++; break;
                case FACE_NEG_X: neighborCoord.x--; break;
                case FACE_POS_Y: neighborCoord.y++; break;
                case FACE_NEG_Y: neighborCoord.y--; break;
                case FACE_POS_Z: neighborCoord.z++; break;
                case FACE_NEG_Z: neighborCoord.z--; break;
            }
            neighborLODs[i] = getChunkLOD(neighborCoord);
        } else {
            neighborLODs[i] = LODLevel::Full;  // No neighbor = assume Full
        }
    }
    
    // Prepare input for smooth mesh kernel
    SmoothMeshInput input;
    input.chunk = chunk;
    for (int i = 0; i < 6; ++i) {
        input.neighbors[i] = neighbors[i];
        input.neighborLODs[i] = neighborLODs[i];
    }
    input.coord = coord;
    input.config = config_.meshConfig;
    input.config.mode = config_.meshMode;
    input.config.lod = chunkLOD;  // Set chunk LOD
    
    // Generate smooth mesh
    SmoothMeshBuffer output;
    smoothMeshKernel_.execute(input, output);
    
    // Notify callback
    if (smoothMeshCallback_ && !output.empty()) {
        smoothMeshCallback_(coord, output);
    }
}

void VoxelWorldV2::setChunkLOD(const ChunkCoord& coord, LODLevel lod) {
    chunkLODs_[coord] = lod;
}

LODLevel VoxelWorldV2::getChunkLOD(const ChunkCoord& coord) const {
    auto it = chunkLODs_.find(coord);
    if (it != chunkLODs_.end()) {
        return it->second;
    }
    return LODLevel::Full;  // Default to Full if not set
}

} // namespace voxel
} // namespace jupiter

