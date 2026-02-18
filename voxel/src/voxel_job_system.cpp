/**
 * @file voxel_job_system.cpp
 * @brief Async job system for voxel geometry generation
 *
 * Implementation based on Venus lock-free ring buffer pattern.
 * Workers continuously process jobs without frame timing constraints.
 */

#include <voxel/voxel_job_system.h>
#include <cstring>
#include <cmath>
#include <glm/gtc/noise.hpp>

namespace jupiter {
namespace voxel {

// ============================================================================
// VoxelJobSystem Implementation
// ============================================================================

VoxelJobSystem::~VoxelJobSystem() {
    if (initialized_) {
        shutdown();
    }
}

bool VoxelJobSystem::initialize(uint32_t numWorkers, uint32_t worldSeed) {
    if (initialized_) {
        return false;
    }

    worldSeed_ = worldSeed;

    // Auto-detect worker count if not specified
    if (numWorkers == 0) {
        numWorkers = std::thread::hardware_concurrency();
        if (numWorkers == 0) {
            numWorkers = 4;  // Fallback
        }
        // Leave some cores for main thread and system
        if (numWorkers > 2) {
            numWorkers = numWorkers - 1;
        }
    }

    // Clamp to max workers
    if (numWorkers > MAX_WORKERS) {
        numWorkers = MAX_WORKERS;
    }

    workerCount_ = numWorkers;

    // Initialize ring buffer
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);

    // Pre-allocate completion queue
    completedQueue_.reserve(256);

    // Create worker contexts
    contexts_.resize(workerCount_);

    for (uint32_t i = 0; i < workerCount_; ++i) {
        auto& ctx = contexts_[i];
        ctx.threadId = i;

        // Create per-worker mesher
        ctx.mesher = std::make_unique<VoxelMesher>();
        ctx.mesher->initialize();

        // Create per-worker voxel data buffers (blocks and lighting)
        ctx.voxelData = std::make_unique<uint8_t[]>(VOXEL_DATA_SIZE);
        ctx.lightingData = std::make_unique<uint8_t[]>(VOXEL_DATA_SIZE);

        // Create per-worker mesh output buffer
        ctx.meshBuffer = std::make_unique<MeshBuffer>();
    }

    // Reset shutdown flag
    shutdown_.store(false, std::memory_order_relaxed);

    // Start worker threads
    workers_.reserve(workerCount_);
    for (uint32_t i = 0; i < workerCount_; ++i) {
        workers_.emplace_back(&VoxelJobSystem::workerLoop, this, i);
    }

    initialized_ = true;
    return true;
}

void VoxelJobSystem::shutdown() {
    if (!initialized_) {
        return;
    }

    // Signal workers to stop
    shutdown_.store(true, std::memory_order_release);

    // Wait for all workers to finish
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Clean up contexts
    for (auto& ctx : contexts_) {
        if (ctx.mesher) {
            ctx.mesher->shutdown();
        }
    }
    contexts_.clear();

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(completedMutex_);
        completedQueue_.clear();
    }

    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);

    initialized_ = false;
}

bool VoxelJobSystem::submitJob(const GeomGenJob& job, int16_t gpuSlot) {
    // Main thread only - no synchronization needed for head access
    // since we're single-producer

    uint64_t seq = head_.load(std::memory_order_relaxed);

    // Check if ring buffer is full (back-pressure)
    uint64_t tailVal = tail_.load(std::memory_order_acquire);
    if (seq - tailVal >= RING_SIZE) {
        // Ring buffer full - drop job (main thread can retry next frame)
        return false;
    }

    // Write job to slot
    auto& slot = ring_[seq & RING_MASK];
    slot.job = job;
    slot.gpuSlot = gpuSlot;

    // Memory fence to ensure job data is visible before publishing
    std::atomic_thread_fence(std::memory_order_release);

    // Publish by incrementing head
    head_.store(seq + 1, std::memory_order_release);

    return true;
}

bool VoxelJobSystem::pollCompleted(CompletedMesh& out) {
    std::lock_guard<std::mutex> lock(completedMutex_);

    if (completedQueue_.empty()) {
        return false;
    }

    // Pop from back (FIFO - we push to back in workers)
    out = std::move(completedQueue_.back());
    completedQueue_.pop_back();
    return true;
}

uint64_t VoxelJobSystem::getPendingCount() const {
    uint64_t h = head_.load(std::memory_order_acquire);
    uint64_t t = tail_.load(std::memory_order_acquire);
    return (h > t) ? (h - t - 1) : 0;
}

uint64_t VoxelJobSystem::getCompletedCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(completedMutex_));
    return completedQueue_.size();
}

// ============================================================================
// Worker Thread
// ============================================================================

void VoxelJobSystem::workerLoop(uint32_t threadId) {
    WorkerContext& ctx = contexts_[threadId];
    uint64_t next = 0;

    while (!shutdown_.load(std::memory_order_relaxed)) {
        // Get current tail (last claimed job)
        uint64_t t = tail_.load(std::memory_order_acquire);
        if (next <= t) {
            next = t + 1;
        }

        // Spin-wait for job to be available
        uint32_t spin = 0;
        while (head_.load(std::memory_order_acquire) <= next) {
            if (shutdown_.load(std::memory_order_relaxed)) {
                return;
            }
            if (++spin > SPIN_COUNT) {
                std::this_thread::yield();
                spin = 0;
            }
        }

        // Try to claim job via CAS
        // We try to update tail from (next - 1) to next
        uint64_t expected = next - 1;
        if (!tail_.compare_exchange_weak(expected, next,
                std::memory_order_release, std::memory_order_relaxed)) {
            // Another worker claimed this job, retry
            continue;
        }

        // We claimed the job - process it
        auto& slot = ring_[next & RING_MASK];
        processJob(ctx, slot.job, slot.gpuSlot);

        ++next;
    }
}

void VoxelJobSystem::processJob(WorkerContext& ctx, const GeomGenJob& job, int16_t gpuSlot) {
    // Generate terrain for this LOD region (blocks and lighting)
    generateLODTerrain(ctx.voxelData.get(), ctx.lightingData.get(), job.bounds, job.level);

    // Mesh the voxel data
    ctx.mesher->setBuffer(ctx.meshBuffer.get());

    // Create temp chunk data from our voxel buffers
    ChunkVoxelData tempChunk;
    const size_t dataSize = PADDED_SIZE * PADDED_HEIGHT * PADDED_SIZE;
    std::memcpy(tempChunk.blocks, ctx.voxelData.get(), dataSize);
    std::memcpy(tempChunk.lighting, ctx.lightingData.get(), dataSize);

    ChunkCoord dummyCoord{0, 0, 0};
    const ChunkVoxelData* neighbors[6] = {nullptr};
    ctx.mesher->beginChunk(&tempChunk, neighbors, dummyCoord);

    MeshResult result = ctx.mesher->meshify();

    // Build completed mesh
    CompletedMesh completed;
    completed.nodeIndex = job.nodeIndex;
    completed.gpuSlot = gpuSlot;
    completed.numVertices = result.numVertices;
    completed.scale = job.scale;
    completed.translate = job.translate;

    // Copy mesh data if there are vertices
    if (result.numVertices > 0) {
        size_t dataSize = result.numVertices * 8;  // 8 bytes per stb vertex
        completed.meshData.resize(dataSize);
        std::memcpy(completed.meshData.data(), result.vertices, dataSize);
    }

    // Push to completed queue (MPSC - workers produce)
    {
        std::lock_guard<std::mutex> lock(completedMutex_);
        completedQueue_.push_back(std::move(completed));
    }
}

// ============================================================================
// Terrain Generation - Varied landscape with mountains, valleys, and hills
// ============================================================================

void VoxelJobSystem::generateLODTerrain(uint8_t* voxelData,
                                         uint8_t* lightingData,
                                         const VisBounds& bounds,
                                         int level) {
    // Clear voxel data and lighting
    const size_t dataSize = PADDED_SIZE * PADDED_HEIGHT * PADDED_SIZE;
    std::memset(voxelData, 0, dataSize);
    std::memset(lightingData, 0, dataSize);  // Air = 0 lighting (for AO)

    // Calculate world-to-voxel scale
    const float boundsWidth = static_cast<float>(bounds.x1 - bounds.x0);
    const float boundsDepth = static_cast<float>(bounds.z1 - bounds.z0);
    const float voxelSizeX = boundsWidth / CHUNK_SIZE;
    const float voxelSizeZ = boundsDepth / CHUNK_SIZE;

    // Sea level and height parameters
    const float seaLevel = 30.0f;         // Base sea level
    const float valleyFloor = 5.0f;       // Deep valleys can go this low
    const float mountainPeak = 150.0f;    // Mountains can reach this high
    const float hillHeight = 60.0f;       // Rolling hills mid-range

    // Generate terrain using multi-octave simplex noise with biome blending
    for (int lx = 0; lx < PADDED_SIZE; ++lx) {
        for (int lz = 0; lz < PADDED_SIZE; ++lz) {
            // World coordinates
            float wx = bounds.x0 + (lx - CHUNK_BORDER) * voxelSizeX;
            float wz = bounds.z0 + (lz - CHUNK_BORDER) * voxelSizeZ;

            // Domain warping - distort coordinates for more organic shapes
            const float warpScale = 0.0008f;
            const float warpStrength = 150.0f;
            glm::vec2 warpPos(wx * warpScale, wz * warpScale);
            float warpX = glm::simplex(warpPos + glm::vec2(100.0f, 0.0f)) * warpStrength;
            float warpZ = glm::simplex(warpPos + glm::vec2(0.0f, 100.0f)) * warpStrength;

            float warpedX = wx + warpX;
            float warpedZ = wz + warpZ;

            // ================================================================
            // Biome selector - determines mountain vs plains vs valley regions
            // ================================================================
            const float biomeScale = 0.0004f;
            glm::vec2 biomePos(warpedX * biomeScale, warpedZ * biomeScale);
            float biomeNoise = glm::simplex(biomePos) * 0.6f
                             + glm::simplex(biomePos * 2.3f) * 0.3f
                             + glm::simplex(biomePos * 4.7f) * 0.1f;

            // Remap to 0-1 range
            biomeNoise = biomeNoise * 0.5f + 0.5f;

            // Create distinct biome regions
            float mountainMask = std::clamp((biomeNoise - 0.6f) * 3.0f, 0.0f, 1.0f);  // Mountains
            float valleyMask = std::clamp((0.35f - biomeNoise) * 3.0f, 0.0f, 1.0f);    // Valleys
            float hillMask = 1.0f - mountainMask - valleyMask;                          // Rolling hills

            // ================================================================
            // Mountain terrain - dramatic peaks with ridges
            // ================================================================
            const float mtScale = 0.002f;
            glm::vec2 mtPos(warpedX * mtScale, warpedZ * mtScale);

            // Ridged noise for mountain peaks (1 - abs creates ridges)
            float ridge1 = 1.0f - std::abs(glm::simplex(mtPos * 0.7f));
            float ridge2 = 1.0f - std::abs(glm::simplex(mtPos * 1.3f + glm::vec2(50.0f)));
            float ridge3 = 1.0f - std::abs(glm::simplex(mtPos * 2.5f + glm::vec2(100.0f)));

            // Sharpen ridges
            ridge1 = ridge1 * ridge1;
            ridge2 = ridge2 * ridge2;
            ridge3 = ridge3 * ridge3;

            // Combine with decreasing weight
            float mountainHeight = ridge1 * 0.6f + ridge2 * 0.3f + ridge3 * 0.15f;
            mountainHeight = std::pow(mountainHeight, 1.5f);  // Extra sharpness
            mountainHeight = seaLevel + mountainHeight * (mountainPeak - seaLevel);

            // ================================================================
            // Valley terrain - erosion patterns, riverbeds
            // ================================================================
            const float valScale = 0.0015f;
            glm::vec2 valPos(warpedX * valScale, warpedZ * valScale);

            // Inverted ridge noise creates valley channels
            float valleyRidge = std::abs(glm::simplex(valPos * 0.5f));
            float valleyRidge2 = std::abs(glm::simplex(valPos * 1.2f + glm::vec2(30.0f)));

            // Combine valley channels
            float valleyChannel = std::min(valleyRidge, valleyRidge2);
            valleyChannel = std::pow(valleyChannel, 0.7f);  // Soften valley bottoms

            // Add some variation
            float valleyVariation = glm::simplex(valPos * 3.0f) * 0.15f;
            float valleyHeight = valleyFloor + valleyChannel * (seaLevel - valleyFloor) + valleyVariation * 10.0f;

            // ================================================================
            // Rolling hills - gentle undulations
            // ================================================================
            const float hillScale = 0.003f;
            glm::vec2 hillPos(warpedX * hillScale, warpedZ * hillScale);

            // Multi-octave for rolling hills
            float hills = glm::simplex(hillPos * 0.3f) * 1.0f;
            hills += glm::simplex(hillPos * 0.7f) * 0.5f;
            hills += glm::simplex(hillPos * 1.5f) * 0.25f;
            hills += glm::simplex(hillPos * 3.0f) * 0.125f;

            // Normalize
            hills = hills / (1.0f + 0.5f + 0.25f + 0.125f);
            hills = hills * 0.5f + 0.5f;  // 0-1 range

            float hillTerrainHeight = seaLevel + hills * (hillHeight - seaLevel);

            // ================================================================
            // Blend biomes together
            // ================================================================
            float height = mountainHeight * mountainMask
                         + valleyHeight * valleyMask
                         + hillTerrainHeight * hillMask;

            // Add micro-detail across all biomes
            const float detailScale = 0.008f;
            glm::vec2 detailPos(wx * detailScale, wz * detailScale);
            float detail = glm::simplex(detailPos) * 3.0f;
            height += detail;

            // Clamp to valid range
            height = std::clamp(height, 1.0f, static_cast<float>(PADDED_HEIGHT - CHUNK_BORDER - 1));
            int heightInt = static_cast<int>(height);

            // ================================================================
            // Fill column with appropriate materials
            // ================================================================
            for (int ly = 0; ly < PADDED_HEIGHT; ++ly) {
                int worldY = ly - CHUNK_BORDER;

                uint8_t block = 0;  // Air

                if (worldY < heightInt) {
                    // Determine block type based on depth and biome
                    int depthFromSurface = heightInt - worldY;

                    if (mountainMask > 0.5f && heightInt > 100) {
                        // High mountain - snow/stone
                        if (depthFromSurface < 2 && heightInt > 120) {
                            block = 4;  // Snow/sand (white appearance)
                        } else {
                            block = 1;  // Stone all the way
                        }
                    } else if (valleyMask > 0.5f && heightInt < 15) {
                        // Valley floor - sand/gravel
                        if (depthFromSurface < 3) {
                            block = 4;  // Sand
                        } else {
                            block = 1;  // Stone
                        }
                    } else {
                        // Normal terrain layering
                        if (depthFromSurface >= 5) {
                            block = 1;  // Stone (deep)
                        } else if (depthFromSurface >= 2) {
                            block = 2;  // Dirt
                        } else {
                            block = 3;  // Grass (surface)
                        }
                    }
                }

                // Index: z varies fastest (stride 1), then y (stride PADDED_SIZE),
                // then x (stride PADDED_HEIGHT*PADDED_SIZE)
                int idx = lx * PADDED_HEIGHT * PADDED_SIZE + ly * PADDED_SIZE + lz;
                voxelData[idx] = block;
                // Lighting: solid=0 (occluded), air=63 (lit) - stb averages for AO
                // Corners surrounded by solid get low values (dark), open faces get high (bright)
                lightingData[idx] = (block != 0) ? 0 : 63;
            }
        }
    }
}

} // namespace voxel
} // namespace jupiter
