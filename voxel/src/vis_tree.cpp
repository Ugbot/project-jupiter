/**
 * @file vis_tree.cpp
 * @brief Implementation of VisTree LOD system
 */

#include <voxel/vis_tree.h>
#include <algorithm>
#include <cmath>

namespace jupiter {
namespace voxel {

void VisTree::initialize(const VisTreeConfig& config) {
    config_ = config;

    // Compute K for screen-space error calculation
    // See: http://tulrich.com/geekstuff/sig-notes.pdf
    K_ = config.displayWidth / (2.0f * std::tan(config.fov * 0.5f));

    // Initialize free list (all nodes available)
    freeNodes_.clear();
    freeNodes_.reserve(MaxNodes);
    for (int i = MaxNodes - 1; i >= 0; --i) {
        freeNodes_.push_back(static_cast<int16_t>(i));
    }

    // Reset all nodes
    for (int i = 0; i < MaxNodes; ++i) {
        nodes_[i].reset();
    }

    // Allocate root node
    rootNode_ = allocNode();
    nodes_[rootNode_].level = static_cast<uint8_t>(config_.maxLevels);

    // Reserve traversal buffers
    traverseStack_.reserve(config_.maxLevels + 1);
    drawList_.reserve(256);
    geomGenJobs_.reserve(config_.maxJobsPerFrame * 2);
    freeGeomSlots_.reserve(64);
}

void VisTree::shutdown() {
    freeNodes_.clear();
    drawList_.clear();
    geomGenJobs_.clear();
    freeGeomSlots_.clear();
    traverseStack_.clear();
    rootNode_ = -1;
}

int16_t VisTree::allocNode() {
    if (freeNodes_.empty()) {
        return -1;  // No nodes available
    }
    int16_t index = freeNodes_.back();
    freeNodes_.pop_back();
    nodes_[index].reset();
    return index;
}

void VisTree::freeNode(int16_t nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= MaxNodes) return;

    VisNode& node = nodes_[nodeIndex];

    // Free GPU slot if allocated
    if (node.gpuSlot >= 0) {
        freeGeomSlots_.push_back(node.gpuSlot);
    }

    node.reset();
    freeNodes_.push_back(nodeIndex);
}

void VisTree::split(int16_t nodeIndex) {
    VisNode& node = nodes_[nodeIndex];
    if (!node.isLeaf()) return;  // Already split

    // Allocate 4 children
    for (int i = 0; i < VisNode::NumChilds; ++i) {
        int16_t childIndex = allocNode();
        if (childIndex < 0) {
            // Failed to allocate - rollback
            for (int j = 0; j < i; ++j) {
                freeNode(node.childs[j]);
                node.childs[j] = VisNode::InvalidChild;
            }
            return;
        }
        node.childs[i] = childIndex;
        nodes_[childIndex].level = node.level > 0 ? node.level - 1 : 0;
    }

    node.clearGeomPending();
}

void VisTree::merge(int16_t nodeIndex) {
    VisNode& node = nodes_[nodeIndex];

    // Recursively free children
    for (int i = 0; i < VisNode::NumChilds; ++i) {
        if (node.childs[i] != VisNode::InvalidChild) {
            // Recursively merge children first
            merge(node.childs[i]);
            freeNode(node.childs[i]);
            node.childs[i] = VisNode::InvalidChild;
        }
    }
}

float VisTree::screenSpaceError(const VisBounds& bounds, int level, float camX, float camZ) const {
    // Geometric error doubles for each level
    const float delta = static_cast<float>(1 << level);

    // Distance from camera to bounds (+1 to avoid division by zero)
    const float D = minDistToBounds(camX, camZ, bounds) + 1.0f;

    // Screen-space error in pixels
    float rho = (delta / D) * K_;
    return rho;
}

VisBounds VisTree::getRootBounds() const {
    // Root covers entire world centered at origin
    const int dim = (1 << config_.maxLevels) * config_.chunkSize;
    VisBounds bounds;
    bounds.x0 = -dim;
    bounds.x1 = dim;
    bounds.z0 = -dim;
    bounds.z1 = dim;
    return bounds;
}

void VisTree::traverse(const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    if (rootNode_ < 0) return;

    // Clear draw list each frame (rebuilt from scratch)
    drawList_.clear();
    traverseStack_.clear();

    // NOTE: Don't clear geomGenJobs_ - they persist across frames until processed
    // Nodes with pending geometry won't request new jobs (needsGeom() checks pending flag)

    // Start traversal from root
    VisBounds rootBounds = getRootBounds();

    // Debug: log root bounds and camera once
    static bool logged = false;
    if (!logged) {
        printf("VisTree: Root bounds [%d,%d] x [%d,%d], camera at (%.1f, %.1f)\n",
               rootBounds.x0, rootBounds.x1, rootBounds.z0, rootBounds.z1,
               cameraPos.x, cameraPos.z);
        logged = true;
    }

    traverseNode(rootNode_, rootBounds, config_.maxLevels,
                 cameraPos.x, cameraPos.z, viewProj);
}

void VisTree::traverseNode(int16_t nodeIndex, const VisBounds& bounds, int level,
                           float camX, float camZ, const glm::mat4& viewProj) {
    if (nodeIndex < 0) return;

    traverseStack_.push_back(nodeIndex);
    VisNode& node = nodes_[nodeIndex];

    // Calculate screen-space error
    float rho = screenSpaceError(bounds, level, camX, camZ);

    // If error is small enough or at max detail, gather this node
    if (rho <= config_.screenSpaceThreshold || level == 0) {
        gatherDrawNode(nodeIndex, level, bounds, viewProj);
    }
    else {
        // Need more detail - split if leaf
        if (node.isLeaf()) {
            split(nodeIndex);
        }

        // Traverse children
        if (!node.isLeaf()) {
            for (int i = 0; i < VisNode::NumChilds; ++i) {
                VisBounds childBounds = bounds.childBounds(i);
                traverseNode(node.childs[i], childBounds, level - 1,
                            camX, camZ, viewProj);
            }
        }
        else {
            // Failed to split - gather this node instead
            gatherDrawNode(nodeIndex, level, bounds, viewProj);
        }
    }

    traverseStack_.pop_back();
}

void VisTree::gatherDrawNode(int16_t nodeIndex, int level, const VisBounds& bounds,
                             const glm::mat4& viewProj) {
    VisNode& node = nodes_[nodeIndex];

    // Height bounds for frustum culling (assume terrain 0-256 units high)
    const float minY = 0.0f;
    const float maxY = 256.0f;

    // Check visibility for drawing (but still generate geometry for non-visible)
    bool visible = isVisible(bounds, minY, maxY, viewProj);

    bool needsPlaceholder = false;

    // ALWAYS queue geometry generation for nodes in LOD range (not just visible ones)
    // This ensures chunks exist when camera turns to face them
    if (!node.hasEmptyGeom() && node.needsGeom()) {
        // Queue geometry generation
        node.setGeomPending();
        glm::vec3 scale = computeScale(bounds, config_.chunkSize);
        glm::vec3 trans = computeTranslation(bounds);
        geomGenJobs_.emplace_back(nodeIndex, level, bounds, scale, trans);
        needsPlaceholder = true;
    }
    else if (node.waitsForGeom()) {
        needsPlaceholder = true;
    }

    // Only add to draw list if visible (frustum cull for rendering only)
    if (!visible) {
        return;
    }

    if (needsPlaceholder) {
        // Try to use children as placeholder (finer detail)
        if (!node.isLeaf() && nodes_[node.childs[0]].hasGeom()) {
            for (int i = 0; i < VisNode::NumChilds; ++i) {
                int16_t childIndex = node.childs[i];
                if (childIndex >= 0) {
                    VisNode& child = nodes_[childIndex];
                    if (child.gpuSlot >= 0 && child.vertexCount > 0) {
                        VisBounds childBounds = bounds.childBounds(i);
                        VisDrawCmd cmd;
                        cmd.nodeIndex = childIndex;
                        cmd.gpuSlot = child.gpuSlot;
                        cmd.scale = computeScale(childBounds, config_.chunkSize);
                        cmd.translate = computeTranslation(childBounds);
                        cmd.vertexCount = child.vertexCount;
                        drawList_.push_back(cmd);
                    }
                }
            }
        }
        // Try parent as placeholder (coarser)
        else if (traverseStack_.size() > 1) {
            int16_t parentIndex = traverseStack_[traverseStack_.size() - 2];
            VisNode& parent = nodes_[parentIndex];
            if (parent.gpuSlot >= 0 && parent.vertexCount > 0) {
                // Parent placeholder - but this can cause z-fighting
                // In production, you'd want to track parent bounds too
            }
        }
    }
    else {
        // Node is ready to draw
        if (!node.hasEmptyGeom() && node.gpuSlot >= 0 && node.vertexCount > 0) {
            VisDrawCmd cmd;
            cmd.nodeIndex = nodeIndex;
            cmd.gpuSlot = node.gpuSlot;
            cmd.scale = computeScale(bounds, config_.chunkSize);
            cmd.translate = computeTranslation(bounds);
            cmd.vertexCount = node.vertexCount;
            drawList_.push_back(cmd);
        }

        // Clean up children if we don't need them anymore
        if (!node.isLeaf()) {
            merge(nodeIndex);
        }
    }
}

bool VisTree::isVisible(const VisBounds& bounds, float minY, float maxY,
                        const glm::mat4& viewProj) const {
    // Simple AABB-frustum test
    // Extract frustum planes from view-projection matrix
    glm::vec4 planes[6];
    const glm::mat4& m = viewProj;

    // Left
    planes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0],
                          m[2][3] + m[2][0], m[3][3] + m[3][0]);
    // Right
    planes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0],
                          m[2][3] - m[2][0], m[3][3] - m[3][0]);
    // Bottom
    planes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1],
                          m[2][3] + m[2][1], m[3][3] + m[3][1]);
    // Top
    planes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1],
                          m[2][3] - m[2][1], m[3][3] - m[3][1]);
    // Near
    planes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2],
                          m[2][3] + m[2][2], m[3][3] + m[3][2]);
    // Far
    planes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2],
                          m[2][3] - m[2][2], m[3][3] - m[3][2]);

    // AABB corners
    glm::vec3 aabbMin(static_cast<float>(bounds.x0), minY, static_cast<float>(bounds.z0));
    glm::vec3 aabbMax(static_cast<float>(bounds.x1), maxY, static_cast<float>(bounds.z1));
    glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
    glm::vec3 halfExtent = (aabbMax - aabbMin) * 0.5f;

    // Test against each plane
    for (int i = 0; i < 6; ++i) {
        glm::vec3 normal(planes[i].x, planes[i].y, planes[i].z);
        float d = planes[i].w;

        // Compute effective radius
        float r = halfExtent.x * std::abs(normal.x) +
                  halfExtent.y * std::abs(normal.y) +
                  halfExtent.z * std::abs(normal.z);

        // Distance from center to plane
        float dist = glm::dot(normal, center) + d;

        if (dist < -r) {
            return false;  // Completely outside this plane
        }
    }

    return true;
}

void VisTree::applyGeom(int16_t nodeIndex, int16_t gpuSlot, uint32_t vertexCount, bool isEmpty) {
    if (nodeIndex < 0 || nodeIndex >= MaxNodes) return;

    VisNode& node = nodes_[nodeIndex];

    if (node.waitsForGeom()) {
        if (isEmpty) {
            node.geoms[0] = VisNode::EmptyGeom;
            node.gpuSlot = -1;
            node.vertexCount = 0;
        }
        else {
            node.geoms[0] = gpuSlot;  // Use gpuSlot as geom index
            node.gpuSlot = gpuSlot;
            node.vertexCount = vertexCount;
        }
        node.clearGeomPending();
    }
    else {
        // Node no longer needs this geometry (was merged away)
        if (gpuSlot >= 0) {
            freeGeomSlots_.push_back(gpuSlot);
        }
    }
}

void VisTree::freeGeom(int16_t gpuSlot) {
    // Find and clear any nodes using this slot
    for (int i = 0; i < MaxNodes; ++i) {
        if (nodes_[i].gpuSlot == gpuSlot) {
            nodes_[i].gpuSlot = -1;
            nodes_[i].geoms[0] = VisNode::InvalidGeom;
            nodes_[i].vertexCount = 0;
        }
    }
}

void VisTree::freeSubtreeGeoms(int16_t nodeIndex) {
    if (nodeIndex < 0) return;

    VisNode& node = nodes_[nodeIndex];

    // Free this node's geometry
    if (node.gpuSlot >= 0) {
        freeGeomSlots_.push_back(node.gpuSlot);
        node.gpuSlot = -1;
        node.geoms[0] = VisNode::InvalidGeom;
        node.vertexCount = 0;
    }

    // Recursively free children
    for (int i = 0; i < VisNode::NumChilds; ++i) {
        if (node.childs[i] != VisNode::InvalidChild) {
            freeSubtreeGeoms(node.childs[i]);
        }
    }
}

} // namespace voxel
} // namespace jupiter
