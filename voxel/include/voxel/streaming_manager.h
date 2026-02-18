#pragma once

#include "voxel_types.h"
#include <memory/queues.h>
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>

/**
 * @file streaming_manager.h
 * @brief Manages chunk loading/unloading based on camera position
 *
 * Following Project Jupiter principles:
 * - Priority-based loading (closer chunks first)
 * - Frame budget control
 * - Lock-free queues for load/unload requests
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Request to load a chunk
 */
struct ChunkLoadRequest {
    ChunkCoord coord;
    float priority;       ///< Lower = higher priority (distance to camera)
    uint8_t lodLevel = 0; ///< Reserved for LOD support

    bool operator>(const ChunkLoadRequest& other) const {
        return priority > other.priority;  // Min-heap ordering
    }
};

/**
 * @brief Frame budget controller for meshing operations
 *
 * Limits meshing time per frame to maintain framerate.
 */
class MeshingBudgeter {
public:
    MeshingBudgeter() = default;

    /**
     * @brief Set target frame time
     * @param ms Target milliseconds per frame (16.67 for 60fps)
     */
    void setTargetFrameTime(float ms) {
        targetFrameMs_ = ms;
    }

    /**
     * @brief Set meshing budget as percentage of frame time
     * @param percentage 0.0 to 1.0 (default 0.15 = 15%)
     */
    void setMeshingBudget(float percentage) {
        meshingBudgetPercent_ = percentage;
    }

    /**
     * @brief Begin frame timing
     */
    void beginFrame() {
        frameMeshTimeMs_ = 0.0f;
        chunksThisFrame_ = 0;
    }

    /**
     * @brief Check if we have budget for another mesh operation
     * @return true if more meshing allowed this frame
     */
    bool canMeshAnother() const {
        const float budgetMs = targetFrameMs_ * meshingBudgetPercent_;
        return frameMeshTimeMs_ < budgetMs;
    }

    /**
     * @brief Record time spent meshing a chunk
     * @param meshTimeMs Milliseconds spent
     */
    void recordMeshing(float meshTimeMs) {
        frameMeshTimeMs_ += meshTimeMs;
        chunksThisFrame_++;

        // Update rolling average
        meshTimeHistory_[historyIndex_] = meshTimeMs;
        historyIndex_ = (historyIndex_ + 1) % HISTORY_SIZE;

        // Recalculate average
        float sum = 0.0f;
        for (float t : meshTimeHistory_) {
            sum += t;
        }
        avgMeshTimeMs_ = sum / HISTORY_SIZE;
    }

    /**
     * @brief Get recommended chunks per frame based on history
     * @return Estimated safe number of chunks to mesh
     */
    uint32_t getRecommendedChunksPerFrame() const {
        if (avgMeshTimeMs_ <= 0.0f) {
            return 4;  // Default
        }
        const float budgetMs = targetFrameMs_ * meshingBudgetPercent_;
        return static_cast<uint32_t>(budgetMs / avgMeshTimeMs_);
    }

    /**
     * @brief Get chunks meshed this frame
     */
    uint32_t getChunksThisFrame() const {
        return chunksThisFrame_;
    }

    /**
     * @brief Get time spent meshing this frame
     */
    float getFrameMeshTimeMs() const {
        return frameMeshTimeMs_;
    }

private:
    static constexpr size_t HISTORY_SIZE = 16;

    float targetFrameMs_ = 16.67f;
    float meshingBudgetPercent_ = 0.15f;

    float avgMeshTimeMs_ = 1.0f;
    float frameMeshTimeMs_ = 0.0f;
    uint32_t chunksThisFrame_ = 0;

    float meshTimeHistory_[HISTORY_SIZE] = {1.0f};
    size_t historyIndex_ = 0;
};

/**
 * @brief Manages chunk streaming based on camera position
 *
 * Determines which chunks should be loaded/unloaded and prioritizes
 * load requests based on distance to camera.
 */
class StreamingManager {
public:
    /// Maximum pending load requests
    // NOTE: This must be large enough to hold all candidates for the chosen
    // view distance; otherwise chunk generation will silently cap early.
    static constexpr size_t MAX_LOAD_QUEUE = 8192;

    /// Maximum pending unload requests
    static constexpr size_t MAX_UNLOAD_QUEUE = 256;

    StreamingManager() = default;
    ~StreamingManager() = default;

    /**
     * @brief Initialize the streaming manager
     * @param viewDistance View distance in chunks
     */
    void initialize(int viewDistance = DEFAULT_VIEW_DISTANCE) {
        viewDistance_ = viewDistance;
        lastCameraChunk_ = ChunkCoord{INT32_MAX, INT32_MAX, INT32_MAX};
        loadRequests_.clear();
        loadRequests_.reserve(MAX_LOAD_QUEUE);
    }

    /**
     * @brief Set view distance
     * @param chunks Distance in chunks
     */
    void setViewDistance(int chunks) {
        viewDistance_ = chunks;
    }

    /**
     * @brief Get view distance
     */
    int getViewDistance() const {
        return viewDistance_;
    }

    /**
     * @brief Update streaming state based on camera position
     *
     * Calculates which chunks should be loaded/unloaded.
     *
     * @param cameraPos Camera world position
     * @param isChunkLoaded Function to check if chunk is loaded
     * @return true if camera moved to new chunk (streaming update needed)
     */
    template<typename IsLoadedFn>
    bool update(const glm::vec3& cameraPos, IsLoadedFn&& isChunkLoaded) {
        const ChunkCoord cameraChunk = worldToChunk(cameraPos);

        bool cameraMovedChunk = (cameraChunk != lastCameraChunk_);

        // Regenerate requests if:
        // 1. Camera moved to new chunk, OR
        // 2. Load queue is empty (need to check for more unloaded chunks)
        bool needsRegeneration = cameraMovedChunk || loadRequests_.empty();

        if (!needsRegeneration) {
            return false;
        }

        lastCameraChunk_ = cameraChunk;

        // Clear previous requests
        loadRequests_.clear();
        unloadRequests_.clear();

        // Generate load requests for chunks in view.
        //
        // Important: for typical terrain worlds, the useful view distance is
        // primarily horizontal (XZ). Generating a full 3D sphere (including Y)
        // explodes candidate chunk counts and burns the request budget on
        // vertical columns you rarely see.
        //
        // We treat viewDistance_ as the horizontal distance, and cap vertical
        // generation to a small band around the camera chunk.
        const int viewDist = viewDistance_;
        const int verticalDist = std::min(viewDist, 4);

        for (int dy = -verticalDist; dy <= verticalDist; ++dy) {
            for (int dz = -viewDist; dz <= viewDist; ++dz) {
                for (int dx = -viewDist; dx <= viewDist; ++dx) {
                    const float horizontalDist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                    if (horizontalDist > static_cast<float>(viewDist)) {
                        continue;
                    }

                    const float dist3D = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));

                    ChunkCoord coord{
                        cameraChunk.x + dx,
                        cameraChunk.y + dy,
                        cameraChunk.z + dz
                    };

                    if (!isChunkLoaded(coord)) {
                        ChunkLoadRequest req;
                        req.coord = coord;
                        req.priority = dist3D;  // Closer = lower priority value
                        loadRequests_.push_back(req);
                    }
                }
            }
        }

        // Sort by priority (closer chunks first)
        std::sort(loadRequests_.begin(), loadRequests_.end(),
            [](const ChunkLoadRequest& a, const ChunkLoadRequest& b) {
                return a.priority < b.priority;
            });

        // Limit queue size
        if (loadRequests_.size() > MAX_LOAD_QUEUE) {
            loadRequests_.resize(MAX_LOAD_QUEUE);
        }

        return cameraMovedChunk;  // Return whether camera actually moved (for unload checks)
    }

    /**
     * @brief Check if chunk is within view distance
     *
     * @param coord Chunk coordinate
     * @param cameraPos Camera world position
     * @return true if within view distance
     */
    bool isInViewDistance(const ChunkCoord& coord, const glm::vec3& cameraPos) const {
        const ChunkCoord cameraChunk = worldToChunk(cameraPos);
        const int dx = coord.x - cameraChunk.x;
        const int dy = coord.y - cameraChunk.y;
        const int dz = coord.z - cameraChunk.z;
        const float dist = std::sqrt(
            static_cast<float>(dx*dx + dy*dy + dz*dz)
        );
        return dist <= viewDistance_;
    }

    /**
     * @brief Pop next load request
     *
     * @param outRequest Output request
     * @return true if request available
     */
    bool popLoadRequest(ChunkLoadRequest& outRequest) {
        if (loadRequests_.empty()) {
            return false;
        }
        outRequest = loadRequests_.back();
        loadRequests_.pop_back();
        return true;
    }

    /**
     * @brief Pop multiple load requests
     *
     * @param outRequests Output array
     * @param maxCount Maximum to pop
     * @return Number popped
     */
    uint32_t popLoadRequests(ChunkLoadRequest* outRequests, uint32_t maxCount) {
        uint32_t count = 0;
        while (count < maxCount && !loadRequests_.empty()) {
            outRequests[count++] = loadRequests_.back();
            loadRequests_.pop_back();
        }
        return count;
    }

    /**
     * @brief Request chunk unload
     *
     * @param coord Chunk to unload
     */
    void requestUnload(const ChunkCoord& coord) {
        unloadQueue_.push(coord);
    }

    /**
     * @brief Pop next unload request
     *
     * @param outCoord Output coordinate
     * @return true if request available
     */
    bool popUnloadRequest(ChunkCoord& outCoord) {
        return unloadQueue_.pop(outCoord);
    }

    /**
     * @brief Get pending load count
     */
    size_t getPendingLoadCount() const {
        return loadRequests_.size();
    }

    /**
     * @brief Get camera chunk
     */
    ChunkCoord getCameraChunk() const {
        return lastCameraChunk_;
    }

private:
    int viewDistance_ = DEFAULT_VIEW_DISTANCE;
    ChunkCoord lastCameraChunk_;

    /// Sorted load requests (priority queue implemented as sorted vector)
    std::vector<ChunkLoadRequest> loadRequests_;

    /// Unload requests (chunks that moved out of view)
    std::vector<ChunkCoord> unloadRequests_;

    /// Lock-free unload queue for thread-safe unload requests
    memory::SPSCQueue<ChunkCoord, MAX_UNLOAD_QUEUE> unloadQueue_;
};

} // namespace voxel
} // namespace jupiter
