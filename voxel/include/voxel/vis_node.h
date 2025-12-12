#pragma once

#include <cstdint>

/**
 * @file vis_node.h
 * @brief Node in the visibility/LOD quadtree
 *
 * Based on Oryol's StbVoxelDemo VisNode.
 * Each node can have geometry and/or 4 children (quadtree).
 */

namespace jupiter {
namespace voxel {

/**
 * @brief A node in the VisTree quadtree
 *
 * Nodes can be:
 * - Leaf nodes: Have geometry, no children
 * - Inner nodes: May have geometry (as placeholder), have 4 children
 * - Pending nodes: Waiting for geometry generation
 * - Empty nodes: Volume is empty (no voxels)
 */
struct VisNode {
    /// Node state flags
    enum Flags : uint16_t {
        None = 0,
        GeomPending = 1 << 0,  ///< Geometry generation in progress
        Visible = 1 << 1,      ///< Was visible last frame
    };

    /// Special geometry indices
    static constexpr int16_t InvalidGeom = -1;  ///< No geometry yet
    static constexpr int16_t EmptyGeom = -2;    ///< Volume is empty
    static constexpr int16_t InvalidChild = -1; ///< No child node

    /// Number of geometry slots (for large meshes that need multiple buffers)
    static constexpr int NumGeoms = 3;

    /// Number of children (quadtree = 4)
    static constexpr int NumChilds = 4;

    /// Node flags
    uint16_t flags = 0;

    /// LOD level (0 = most detailed, higher = coarser)
    uint8_t level = 0;

    /// Padding for alignment
    uint8_t _padding = 0;

    /// Geometry indices (into GeomPool)
    int16_t geoms[NumGeoms] = {InvalidGeom, InvalidGeom, InvalidGeom};

    /// Child node indices (quadtree)
    int16_t childs[NumChilds] = {InvalidChild, InvalidChild, InvalidChild, InvalidChild};

    /// GPU chunk slot index (for rendering)
    int16_t gpuSlot = -1;

    /// Vertex count for this node's geometry
    uint32_t vertexCount = 0;

    /// Reset node to initial state
    void reset() {
        flags = 0;
        level = 0;
        for (int i = 0; i < NumGeoms; ++i) geoms[i] = InvalidGeom;
        for (int i = 0; i < NumChilds; ++i) childs[i] = InvalidChild;
        gpuSlot = -1;
        vertexCount = 0;
    }

    /// Check if this is a leaf node (no children)
    bool isLeaf() const {
        return childs[0] == InvalidChild;
    }

    /// Check if node has valid geometry
    bool hasGeom() const {
        return geoms[0] != InvalidGeom && geoms[0] != EmptyGeom;
    }

    /// Check if geometry slot has valid index
    bool hasValidGeom(int slot = 0) const {
        return geoms[slot] >= 0;
    }

    /// Check if volume is known to be empty
    bool hasEmptyGeom() const {
        return geoms[0] == EmptyGeom;
    }

    /// Check if node needs geometry generated
    bool needsGeom() const {
        return geoms[0] == InvalidGeom && !(flags & GeomPending);
    }

    /// Check if waiting for geometry
    bool waitsForGeom() const {
        return (flags & GeomPending) != 0;
    }

    /// Mark geometry as pending
    void setGeomPending() {
        flags |= GeomPending;
    }

    /// Clear geometry pending flag
    void clearGeomPending() {
        flags &= ~GeomPending;
    }

    /// Set visibility flag
    void setVisible(bool visible) {
        if (visible) {
            flags |= Visible;
        } else {
            flags &= ~Visible;
        }
    }

    /// Check if was visible
    bool wasVisible() const {
        return (flags & Visible) != 0;
    }
};

static_assert(sizeof(VisNode) <= 32, "VisNode should be compact");

} // namespace voxel
} // namespace jupiter
