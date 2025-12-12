#pragma once

#include "vis_node.h"
#include "vis_bounds.h"
#include "voxel_types.h"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <vector>
#include <cmath>
#include <functional>

/**
 * @file vis_tree.h
 * @brief Sparse quadtree for LOD and visibility management
 *
 * Based on Oryol's StbVoxelDemo VisTree.
 * Uses screen-space error metric to decide LOD level.
 *
 * Key concepts:
 * - Level 0 = most detailed (32x32 voxel chunks)
 * - Higher levels = coarser (same voxel grid covers larger world area)
 * - Screen-space error determines when to split/merge
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Job for async geometry generation
 */
struct GeomGenJob {
    int16_t nodeIndex = -1;   ///< Node to generate geometry for
    int8_t level = 0;         ///< LOD level
    int8_t _padding = 0;
    VisBounds bounds;         ///< World bounds for this node
    glm::vec3 scale;          ///< Scale for rendering
    glm::vec3 translate;      ///< Translation for rendering

    GeomGenJob() = default;
    GeomGenJob(int16_t node, int lvl, const VisBounds& b, const glm::vec3& s, const glm::vec3& t)
        : nodeIndex(node), level(static_cast<int8_t>(lvl)), bounds(b), scale(s), translate(t) {}
};

/**
 * @brief Draw command for a single node
 */
struct VisDrawCmd {
    int16_t nodeIndex;        ///< Node to draw
    int16_t gpuSlot;          ///< GPU chunk slot
    glm::vec3 scale;          ///< Scale for this LOD level
    glm::vec3 translate;      ///< World position
    uint32_t vertexCount;     ///< Number of vertices
};

/**
 * @brief Configuration for VisTree
 */
struct VisTreeConfig {
    int displayWidth = 1920;           ///< Display width for screen-space error
    float fov = 1.047f;                ///< Field of view in radians (~60 degrees)
    float screenSpaceThreshold = 15.0f;///< Pixel threshold for LOD decision
    int chunkSize = 32;                ///< Voxel resolution per chunk
    int maxLevels = 8;                 ///< Maximum LOD levels
    int maxNodes = 4096;               ///< Maximum nodes in tree
    int maxJobsPerFrame = 4;           ///< Max geometry jobs per frame
};

/**
 * @brief Sparse quadtree for voxel LOD and visibility
 */
class VisTree {
public:
    /// Maximum nodes (increased for larger draw distance)
    static constexpr int MaxNodes = 8192;

    /// Maximum LOD levels
    static constexpr int MaxLevels = 10;

    VisTree() = default;
    ~VisTree() = default;

    /**
     * @brief Initialize the visibility tree
     * @param config Configuration parameters
     */
    void initialize(const VisTreeConfig& config);

    /**
     * @brief Shutdown and free resources
     */
    void shutdown();

    /**
     * @brief Traverse tree and determine what to draw
     *
     * @param cameraPos Camera world position
     * @param viewProj View-projection matrix (for frustum culling)
     */
    void traverse(const glm::vec3& cameraPos, const glm::mat4& viewProj);

    /**
     * @brief Get nodes to draw this frame
     */
    const std::vector<VisDrawCmd>& getDrawList() const { return drawList_; }

    /**
     * @brief Get pending geometry generation jobs
     */
    std::vector<GeomGenJob>& getGeomGenJobs() { return geomGenJobs_; }

    /**
     * @brief Apply generated geometry to a node
     *
     * @param nodeIndex Node index
     * @param gpuSlot GPU chunk slot index
     * @param vertexCount Number of vertices
     * @param isEmpty True if volume was empty
     */
    void applyGeom(int16_t nodeIndex, int16_t gpuSlot, uint32_t vertexCount, bool isEmpty = false);

    /**
     * @brief Mark geometry as freed (GPU slot released)
     * @param gpuSlot GPU slot that was freed
     */
    void freeGeom(int16_t gpuSlot);

    /**
     * @brief Get node by index
     */
    VisNode& nodeAt(int16_t index) { return nodes_[index]; }
    const VisNode& nodeAt(int16_t index) const { return nodes_[index]; }

    /**
     * @brief Get statistics
     */
    int getActiveNodeCount() const { return MaxNodes - static_cast<int>(freeNodes_.size()); }
    int getPendingJobCount() const { return static_cast<int>(geomGenJobs_.size()); }
    int getDrawCount() const { return static_cast<int>(drawList_.size()); }

    /**
     * @brief Get and clear freed GPU slots (for releasing GPU resources)
     */
    std::vector<int16_t>& getFreeGeomSlots() { return freeGeomSlots_; }

    /**
     * @brief Get root bounds (entire world)
     */
    VisBounds getRootBounds() const;

private:
    /// Allocate a new node
    int16_t allocNode();

    /// Free a node (and its geometry)
    void freeNode(int16_t nodeIndex);

    /// Split a leaf node into 4 children
    void split(int16_t nodeIndex);

    /// Merge children back into parent (recursive)
    void merge(int16_t nodeIndex);

    /// Compute screen-space error for a node
    float screenSpaceError(const VisBounds& bounds, int level, float camX, float camZ) const;

    /// Recursive traversal
    void traverseNode(int16_t nodeIndex, const VisBounds& bounds, int level,
                      float camX, float camZ, const glm::mat4& viewProj);

    /// Gather node for drawing (handles placeholders)
    void gatherDrawNode(int16_t nodeIndex, int level, const VisBounds& bounds,
                        const glm::mat4& viewProj);

    /// Check if bounds are visible in frustum
    bool isVisible(const VisBounds& bounds, float minY, float maxY, const glm::mat4& viewProj) const;

    /// Free all geometry in subtree
    void freeSubtreeGeoms(int16_t nodeIndex);

    // Configuration
    VisTreeConfig config_;
    float K_ = 0.0f;  ///< Screen-space error constant

    // Node storage
    VisNode nodes_[MaxNodes];
    std::vector<int16_t> freeNodes_;
    int16_t rootNode_ = -1;

    // Traversal state
    std::vector<int16_t> traverseStack_;
    std::vector<VisDrawCmd> drawList_;
    std::vector<GeomGenJob> geomGenJobs_;
    std::vector<int16_t> freeGeomSlots_;  ///< GPU slots to free
};

} // namespace voxel
} // namespace jupiter
