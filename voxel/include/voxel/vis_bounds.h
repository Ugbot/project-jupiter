#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

/**
 * @file vis_bounds.h
 * @brief 2D bounding rectangle for VisTree nodes (XZ plane)
 *
 * Based on Oryol's StbVoxelDemo VisBounds.
 * Y (height) is handled separately since voxel worlds are typically flat.
 */

namespace jupiter {
namespace voxel {

/**
 * @brief 2D bounding rectangle in world coordinates (XZ plane)
 */
struct VisBounds {
    int32_t x0 = 0;  ///< Min X (world units)
    int32_t x1 = 0;  ///< Max X (world units)
    int32_t z0 = 0;  ///< Min Z (world units)
    int32_t z1 = 0;  ///< Max Z (world units)

    /// Get width in world units
    int32_t width() const { return x1 - x0; }

    /// Get depth in world units
    int32_t depth() const { return z1 - z0; }

    /// Get center X
    float centerX() const { return (x0 + x1) * 0.5f; }

    /// Get center Z
    float centerZ() const { return (z0 + z1) * 0.5f; }

    /// Check if point is inside bounds (XZ only)
    bool contains(float x, float z) const {
        return x >= x0 && x < x1 && z >= z0 && z < z1;
    }

    /// Get child bounds for quadtree subdivision
    /// childIndex: 0=(-x,-z), 1=(+x,-z), 2=(-x,+z), 3=(+x,+z)
    VisBounds childBounds(int childIndex) const {
        const int32_t halfW = (x1 - x0) / 2;
        const int32_t halfD = (z1 - z0) / 2;
        const int xi = childIndex & 1;
        const int zi = (childIndex >> 1) & 1;

        VisBounds child;
        child.x0 = x0 + xi * halfW;
        child.x1 = child.x0 + halfW;
        child.z0 = z0 + zi * halfD;
        child.z1 = child.z0 + halfD;
        return child;
    }

    bool operator==(const VisBounds& other) const {
        return x0 == other.x0 && x1 == other.x1 && z0 == other.z0 && z1 == other.z1;
    }
};

/**
 * @brief Compute bounds for a given LOD level and position
 *
 * @param level LOD level (0 = most detailed)
 * @param chunkX Chunk X coordinate at level 0
 * @param chunkZ Chunk Z coordinate at level 0
 * @param chunkSize Base chunk size in world units
 * @return Bounds for this node
 */
inline VisBounds computeBounds(int level, int chunkX, int chunkZ, int chunkSize = 32) {
    // At level N, each node covers 2^N chunks worth of space
    const int scale = 1 << level;
    const int dim = scale * chunkSize;

    // Align to level grid
    const int alignedX = (chunkX >> level) * dim;
    const int alignedZ = (chunkZ >> level) * dim;

    VisBounds bounds;
    bounds.x0 = alignedX;
    bounds.x1 = alignedX + dim;
    bounds.z0 = alignedZ;
    bounds.z1 = alignedZ + dim;
    return bounds;
}

/**
 * @brief Compute minimum distance from point to bounds
 *
 * @param x Camera X position
 * @param z Camera Z position
 * @param bounds Bounds to check
 * @return Minimum distance (0 if inside)
 */
inline float minDistToBounds(float x, float z, const VisBounds& bounds) {
    float dx = 0.0f;
    float dz = 0.0f;

    if (x < bounds.x0) {
        dx = bounds.x0 - x;
    } else if (x > bounds.x1) {
        dx = x - bounds.x1;
    }

    if (z < bounds.z0) {
        dz = bounds.z0 - z;
    } else if (z > bounds.z1) {
        dz = z - bounds.z1;
    }

    return std::sqrt(dx * dx + dz * dz);
}

/**
 * @brief Compute scale vector for rendering a chunk at given bounds
 *
 * The same 32x32 voxel grid is stretched to cover the bounds.
 *
 * @param bounds World bounds
 * @param chunkSize Voxel resolution (32)
 * @return Scale vector (x, 1, z) - Y scale is always 1
 */
inline glm::vec3 computeScale(const VisBounds& bounds, int chunkSize = 32) {
    return glm::vec3(
        static_cast<float>(bounds.x1 - bounds.x0) / chunkSize,
        1.0f,  // Y (height) doesn't scale with LOD
        static_cast<float>(bounds.z1 - bounds.z0) / chunkSize
    );
}

/**
 * @brief Compute translation vector for rendering
 *
 * @param bounds World bounds
 * @return Translation to world position
 */
inline glm::vec3 computeTranslation(const VisBounds& bounds) {
    return glm::vec3(
        static_cast<float>(bounds.x0),
        0.0f,
        static_cast<float>(bounds.z0)
    );
}

} // namespace voxel
} // namespace jupiter
