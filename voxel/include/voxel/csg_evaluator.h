#pragma once

/**
 * @file csg_evaluator.h
 * @brief CSG (Constructive Solid Geometry) evaluator for voxel editing
 *
 * Evaluates CSG primitives and trees against voxel chunk data using
 * Signed Distance Functions (SDFs) for smooth geometry representation.
 */

#include "csg_types.h"
#include "voxel_types.h"
#include <glm/glm.hpp>

namespace jupiter {
namespace voxel {

// Forward declaration
class ChunkColumns;

/**
 * @brief Evaluator for CSG operations on voxel data
 *
 * Uses Signed Distance Functions (SDFs) to evaluate whether voxels
 * are inside/outside primitives, then applies CSG operations.
 */
class CSGEvaluator {
public:
    CSGEvaluator() = default;
    ~CSGEvaluator() = default;
    
    // ========================================================================
    // Primitive Evaluation
    // ========================================================================
    
    /**
     * @brief Evaluate a single CSG primitive against a chunk
     *
     * Iterates all voxels in the chunk, computes SDF value for each,
     * and applies the CSG operation.
     *
     * @param primitive The CSG primitive to evaluate
     * @param blocks Block data array (VOXEL_DATA_SIZE elements)
     * @param chunkCoord Chunk coordinate for world position calculation
     */
    void evaluate(const CSGPrimitive& primitive,
                  BlockType* blocks,
                  const ChunkCoord& chunkCoord);
    
    /**
     * @brief Evaluate a single CSG primitive against columnar chunk storage
     *
     * Works directly on ChunkColumns without intermediate copy.
     *
     * @param primitive The CSG primitive to evaluate
     * @param chunk ChunkColumns to modify
     * @param chunkCoord Chunk coordinate for world position calculation
     */
    void evaluate(const CSGPrimitive& primitive,
                  ChunkColumns& chunk,
                  const ChunkCoord& chunkCoord);
    
    /**
     * @brief Evaluate a CSG tree against a chunk
     *
     * Recursively evaluates the CSG tree, combining results with
     * boolean operations.
     *
     * @param root Root node of the CSG tree
     * @param blocks Block data array
     * @param chunkCoord Chunk coordinate
     */
    void evaluateTree(const CSGNode* root,
                      BlockType* blocks,
                      const ChunkCoord& chunkCoord);
    
    // ========================================================================
    // Signed Distance Functions (SDFs)
    // ========================================================================
    
    /**
     * @brief SDF for axis-aligned box
     *
     * @param p Point in local primitive space
     * @param halfExtents Half-size of box in each dimension
     * @return Signed distance (negative = inside)
     */
    static float sdfBox(const glm::vec3& p, const glm::vec3& halfExtents);
    
    /**
     * @brief SDF for sphere
     *
     * @param p Point in local primitive space
     * @param radius Sphere radius
     * @return Signed distance (negative = inside)
     */
    static float sdfSphere(const glm::vec3& p, float radius);
    
    /**
     * @brief SDF for cylinder (Y-axis aligned)
     *
     * @param p Point in local primitive space
     * @param radius Cylinder radius
     * @param height Total height
     * @return Signed distance (negative = inside)
     */
    static float sdfCylinder(const glm::vec3& p, float radius, float height);
    
    /**
     * @brief SDF for capsule (Y-axis aligned)
     *
     * @param p Point in local primitive space
     * @param radius Capsule radius
     * @param height Total height
     * @return Signed distance (negative = inside)
     */
    static float sdfCapsule(const glm::vec3& p, float radius, float height);
    
    /**
     * @brief SDF for cone (Y-axis aligned, tip at top)
     *
     * @param p Point in local primitive space
     * @param radius Base radius
     * @param height Total height
     * @return Signed distance (negative = inside)
     */
    static float sdfCone(const glm::vec3& p, float radius, float height);
    
    /**
     * @brief SDF for torus (XZ plane)
     *
     * @param p Point in local primitive space
     * @param majorRadius Distance from center to tube center
     * @param minorRadius Radius of the tube
     * @return Signed distance (negative = inside)
     */
    static float sdfTorus(const glm::vec3& p, float majorRadius, float minorRadius);
    
    /**
     * @brief SDF for half-space plane
     *
     * @param p Point in world space
     * @param normal Plane normal (points into solid region)
     * @param distance Distance from origin along normal
     * @return Signed distance (negative = inside/below plane)
     */
    static float sdfPlane(const glm::vec3& p, const glm::vec3& normal, float distance);
    
    // ========================================================================
    // SDF Operations
    // ========================================================================
    
    /**
     * @brief SDF union (minimum of two distances)
     */
    static float sdfUnion(float d1, float d2) {
        return glm::min(d1, d2);
    }
    
    /**
     * @brief SDF difference (d1 - d2)
     */
    static float sdfDifference(float d1, float d2) {
        return glm::max(d1, -d2);
    }
    
    /**
     * @brief SDF intersection (maximum of two distances)
     */
    static float sdfIntersection(float d1, float d2) {
        return glm::max(d1, d2);
    }
    
    /**
     * @brief Smooth union for blending shapes
     */
    static float sdfSmoothUnion(float d1, float d2, float k) {
        float h = glm::clamp(0.5f + 0.5f * (d2 - d1) / k, 0.0f, 1.0f);
        return glm::mix(d2, d1, h) - k * h * (1.0f - h);
    }
    
    /**
     * @brief Smooth difference for blending shapes
     */
    static float sdfSmoothDifference(float d1, float d2, float k) {
        float h = glm::clamp(0.5f - 0.5f * (d2 + d1) / k, 0.0f, 1.0f);
        return glm::mix(d1, -d2, h) + k * h * (1.0f - h);
    }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set the surface threshold for voxelization
     *
     * Voxels with SDF value <= threshold are considered solid.
     * Default is 0.0 (exact surface).
     */
    void setSurfaceThreshold(float threshold) {
        surfaceThreshold_ = threshold;
    }
    
    /**
     * @brief Set smooth blending factor for smooth CSG ops
     *
     * Higher values = more blending. Default is 0 (sharp edges).
     */
    void setSmoothFactor(float factor) {
        smoothFactor_ = factor;
    }
    
private:
    /**
     * @brief Evaluate the SDF for a primitive at a point
     */
    float evaluatePrimitiveSDF(const CSGPrimitive& primitive,
                               const glm::vec3& worldPoint) const;
    
    /**
     * @brief Recursively evaluate a CSG tree node
     */
    float evaluateNodeSDF(const CSGNode* node,
                          const glm::vec3& worldPoint) const;
    
    /**
     * @brief Apply a CSG operation at a single voxel
     *
     * @param op The CSG operation
     * @param material Material to use for solid regions
     * @param existing Current block type
     * @param sdfValue SDF value at this voxel
     */
    void applyOperation(CSGOperation op,
                        BlockType material,
                        BlockType& existing,
                        float sdfValue);
    
    /// Surface threshold for voxelization
    float surfaceThreshold_ = 0.0f;
    
    /// Smooth blending factor (0 = sharp)
    float smoothFactor_ = 0.0f;
};

} // namespace voxel
} // namespace jupiter

