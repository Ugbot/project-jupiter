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

        // Create per-worker voxel data buffer
        ctx.voxelData = std::make_unique<uint8_t[]>(VOXEL_DATA_SIZE);

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
    // Generate terrain for this LOD region
    generateLODTerrain(ctx.voxelData.get(), job.bounds, job.level);

    // Mesh the voxel data
    ctx.mesher->setBuffer(ctx.meshBuffer.get());

    // Create temp chunk data from our voxel buffer
    ChunkVoxelData tempChunk;
    std::memcpy(tempChunk.blocks, ctx.voxelData.get(),
                PADDED_SIZE * PADDED_HEIGHT * PADDED_SIZE);

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
// Terrain Generation
// ============================================================================

void VoxelJobSystem::generateLODTerrain(uint8_t* voxelData,
                                         const VisBounds& bounds,
                                         int level) {
    // Clear voxel data
    std::memset(voxelData, 0, PADDED_SIZE * PADDED_HEIGHT * PADDED_SIZE);

    // Calculate world-to-voxel scale
    const float boundsWidth = static_cast<float>(bounds.x1 - bounds.x0);
    const float boundsDepth = static_cast<float>(bounds.z1 - bounds.z0);
    const float voxelSizeX = boundsWidth / CHUNK_SIZE;
    const float voxelSizeZ = boundsDepth / CHUNK_SIZE;

    const float noiseScale = 0.003f;  // Larger features for LOD
    const float amplitude = 80.0f;    // Tall hills
    const float baseHeight = 8.0f;    // Base height

    // Generate terrain using multi-octave simplex noise
    for (int lx = 0; lx < PADDED_SIZE; ++lx) {
        for (int lz = 0; lz < PADDED_SIZE; ++lz) {
            // World coordinates
            float wx = bounds.x0 + (lx - CHUNK_BORDER) * voxelSizeX;
            float wz = bounds.z0 + (lz - CHUNK_BORDER) * voxelSizeZ;

            // Multi-octave simplex noise with variation
            glm::vec2 p(wx * noiseScale, wz * noiseScale);

            // Large rolling hills
            float n = glm::simplex(p * 0.3f) * 1.0f;
            // Medium features
            n += glm::simplex(p * 0.7f) * 0.6f;
            // Small hills
            n += glm::simplex(p * 1.5f) * 0.35f;
            // Fine detail
            n += glm::simplex(p * 3.0f) * 0.2f;
            // Very fine detail
            n += glm::simplex(p * 6.0f) * 0.1f;

            // Ridge noise for more dramatic terrain
            float ridge = 1.0f - std::abs(glm::simplex(p * 0.5f + glm::vec2(100.0f)));
            ridge = ridge * ridge;  // Sharpen ridges
            n += ridge * 0.4f;

            // Normalize to roughly 0-1
            n = n * 0.35f + 0.5f;
            n = std::clamp(n, 0.0f, 1.0f);

            int height = static_cast<int>(baseHeight + n * amplitude);

            // Fill column (use PADDED_HEIGHT for Y dimension)
            for (int ly = 0; ly < PADDED_HEIGHT; ++ly) {
                int worldY = ly - CHUNK_BORDER;

                uint8_t block = 0;  // Air
                if (worldY < height - 3) {
                    block = 1;  // Stone
                } else if (worldY < height - 1) {
                    block = 2;  // Dirt
                } else if (worldY < height) {
                    block = 3;  // Grass
                }

                // Index: z varies fastest (stride 1), then y (stride PADDED_SIZE),
                // then x (stride PADDED_HEIGHT*PADDED_SIZE)
                int idx = lx * PADDED_HEIGHT * PADDED_SIZE + ly * PADDED_SIZE + lz;
                voxelData[idx] = block;
            }
        }
    }
}

} // namespace voxel
} // namespace jupiter
