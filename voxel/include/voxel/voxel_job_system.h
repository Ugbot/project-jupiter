#pragma once

#include "vis_tree.h"
#include "voxel_mesher.h"
#include "mesh_buffer_pool.h"
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>

/**
 * @file voxel_job_system.h
 * @brief Async job system for voxel geometry generation
 *
 * Following Project Jupiter principles:
 * - Lock-free SPMC ring buffer for job submission (single producer, multiple consumer)
 * - Lock-free MPSC queue for completed meshes (multiple producer, single consumer)
 * - Per-worker resources (no sharing between threads)
 * - No runtime allocations in hot paths
 * - Continuous async processing (not frame-gated)
 *
 * Architecture:
 * - Main thread submits GeomGenJobs via submitJob() (lock-free)
 * - Worker threads continuously process jobs and push completed meshes
 * - Main thread polls completions via pollCompleted() (lock-free)
 * - GPU uploads happen on main thread after polling
 *
 * Based on Venus job system (lock-free ring buffer) and Arrow async patterns.
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Result of async geometry generation
 *
 * Workers fill this with mesh data, main thread polls and uploads to GPU.
 */
struct CompletedMesh {
    int16_t nodeIndex = -1;          ///< VisTree node index
    int16_t gpuSlot = -1;            ///< Pre-allocated GPU slot
    uint32_t numVertices = 0;        ///< Number of vertices generated
    glm::vec3 scale{1.0f};           ///< Scale for rendering
    glm::vec3 translate{0.0f};       ///< Translation for rendering

    /// Mesh data copied from worker buffer (main thread uploads to GPU)
    /// Using vector here is acceptable since it's only touched in the completion
    /// path, not the hot meshing path.
    std::vector<uint8_t> meshData;
};

/**
 * @brief Internal job slot in ring buffer
 */
struct alignas(64) JobSlot {
    GeomGenJob job;
    int16_t gpuSlot = -1;
    int16_t _padding = 0;
};

/**
 * @brief Async job system for voxel geometry generation
 *
 * Uses a lock-free SPMC ring buffer for job distribution and continuous
 * worker threads that process jobs asynchronously (not gated on frames).
 *
 * Thread Model:
 * - Producer: Main thread only (SPSC for submission)
 * - Consumers: N worker threads (SPMC for claiming)
 * - Completion: MPSC (workers push, main polls)
 */
class VoxelJobSystem {
public:
    /// Ring buffer size (must be power of 2)
    static constexpr uint32_t RING_SIZE = 16384;
    static constexpr uint32_t RING_MASK = RING_SIZE - 1;

    /// Maximum worker threads
    static constexpr uint32_t MAX_WORKERS = 16;

    /// Spin count before yielding
    static constexpr uint32_t SPIN_COUNT = 1000;

    VoxelJobSystem() = default;
    ~VoxelJobSystem();

    // Non-copyable, non-movable
    VoxelJobSystem(const VoxelJobSystem&) = delete;
    VoxelJobSystem& operator=(const VoxelJobSystem&) = delete;
    VoxelJobSystem(VoxelJobSystem&&) = delete;
    VoxelJobSystem& operator=(VoxelJobSystem&&) = delete;

    /**
     * @brief Initialize the job system with worker threads
     *
     * @param numWorkers Number of worker threads (0 = auto-detect based on hardware)
     * @param worldSeed World generation seed for terrain
     * @return true if initialization successful
     */
    bool initialize(uint32_t numWorkers = 0, uint32_t worldSeed = 12345);

    /**
     * @brief Shutdown the job system
     *
     * Signals workers to stop and waits for them to finish.
     */
    void shutdown();

    /**
     * @brief Submit a geometry generation job (main thread only)
     *
     * Lock-free submission to ring buffer.
     *
     * @param job The geometry generation job
     * @param gpuSlot Pre-allocated GPU slot for results
     * @return true if job was submitted, false if ring buffer full
     */
    bool submitJob(const GeomGenJob& job, int16_t gpuSlot);

    /**
     * @brief Poll for completed meshes (main thread only)
     *
     * Lock-free retrieval from completion queue.
     *
     * @param out Output completed mesh (if available)
     * @return true if a completed mesh was retrieved
     */
    bool pollCompleted(CompletedMesh& out);

    /**
     * @brief Get number of pending jobs in ring buffer
     */
    uint64_t getPendingCount() const;

    /**
     * @brief Get number of completed meshes waiting
     */
    uint64_t getCompletedCount() const;

    /**
     * @brief Get number of active worker threads
     */
    uint32_t getWorkerCount() const { return workerCount_; }

    /**
     * @brief Check if job system is running
     */
    bool isRunning() const { return !shutdown_.load(std::memory_order_relaxed); }

private:
    /**
     * @brief Per-worker thread context
     *
     * Each worker has its own resources to avoid sharing/locking.
     * This is the key to high throughput - no contention on mesher state.
     */
    struct WorkerContext {
        /// Mesher instance for this worker
        std::unique_ptr<VoxelMesher> mesher;

        /// Pre-allocated voxel data buffer (VOXEL_DATA_SIZE bytes)
        std::unique_ptr<uint8_t[]> voxelData;

        /// Pre-allocated lighting data buffer for AO (VOXEL_DATA_SIZE bytes)
        std::unique_ptr<uint8_t[]> lightingData;

        /// Pre-allocated mesh output buffer
        std::unique_ptr<MeshBuffer> meshBuffer;

        /// Thread ID for debugging
        uint32_t threadId = 0;
    };

    /**
     * @brief Worker thread main loop
     *
     * Spin-waits for jobs, claims via CAS, processes, pushes completions.
     *
     * @param threadId Worker thread index
     */
    void workerLoop(uint32_t threadId);

    /**
     * @brief Process a single job
     *
     * Generates terrain, meshes it, creates CompletedMesh.
     *
     * @param ctx Worker context with resources
     * @param job Job to process
     * @param gpuSlot Pre-allocated GPU slot
     */
    void processJob(WorkerContext& ctx, const GeomGenJob& job, int16_t gpuSlot);

    /**
     * @brief Generate LOD terrain data for a bounds
     *
     * @param voxelData Output voxel buffer (blocks)
     * @param lightingData Output lighting buffer (for AO computation)
     * @param bounds World bounds to generate
     * @param level LOD level
     */
    void generateLODTerrain(uint8_t* voxelData, uint8_t* lightingData, const VisBounds& bounds, int level);

    // =========================================================================
    // Ring Buffer (SPMC - Single Producer Multiple Consumer)
    // =========================================================================

    /// Job ring buffer (cache-line aligned slots)
    JobSlot ring_[RING_SIZE];

    /// Producer sequence (main thread writes)
    alignas(64) std::atomic<uint64_t> head_{0};

    /// Consumer sequence (workers claim via CAS)
    alignas(64) std::atomic<uint64_t> tail_{0};

    // =========================================================================
    // Completion Queue (MPSC - Multiple Producer Single Consumer)
    // =========================================================================
    // Using mutex for now - can optimize to lock-free later if needed.
    // This is NOT in the critical meshing path, only touched after job completion.

    std::mutex completedMutex_;
    std::vector<CompletedMesh> completedQueue_;

    // =========================================================================
    // Worker State
    // =========================================================================

    /// Worker threads
    std::vector<std::thread> workers_;

    /// Per-worker contexts (one per thread)
    std::vector<WorkerContext> contexts_;

    /// Number of workers
    uint32_t workerCount_ = 0;

    /// Shutdown signal
    std::atomic<bool> shutdown_{false};

    /// World seed for terrain generation
    uint32_t worldSeed_ = 12345;

    /// Initialization state
    bool initialized_ = false;
};

} // namespace voxel
} // namespace jupiter
