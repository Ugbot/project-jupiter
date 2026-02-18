#pragma once

/**
 * @file csg_types.h
 * @brief CSG (Constructive Solid Geometry) types for voxel editing
 *
 * Provides primitives and operations for constructive solid geometry
 * on voxel volumes: union, difference, intersection.
 */

#include "voxel_types.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

namespace jupiter {
namespace voxel {

// ============================================================================
// CSG Operations
// ============================================================================

/**
 * @brief CSG boolean operations
 */
enum class CSGOperation : uint8_t {
    Union,          ///< Add material where primitive is solid (OR)
    Difference,     ///< Remove material where primitive is solid (AND NOT)
    Intersection,   ///< Keep only where both are solid (AND)
    Replace,        ///< Overwrite with primitive's material unconditionally
};

/**
 * @brief CSG primitive types
 */
enum class CSGPrimitiveType : uint8_t {
    Box,            ///< Axis-aligned box (uses scale as half-extents)
    Sphere,         ///< Sphere
    Cylinder,       ///< Cylinder (Y-axis aligned before rotation)
    Capsule,        ///< Capsule (cylinder with hemispherical caps)
    Cone,           ///< Cone
    Torus,          ///< Torus (donut shape)
    Plane,          ///< Infinite half-space (normal points into solid)
    Heightmap,      ///< Height field from 2D array
    VoxelMask,      ///< Use another chunk as a mask
};

// ============================================================================
// CSG Primitive Parameters
// ============================================================================

/**
 * @brief Parameters for sphere primitive
 */
struct CSGSphereParams {
    float radius = 1.0f;
};

/**
 * @brief Parameters for cylinder primitive
 */
struct CSGCylinderParams {
    float radius = 1.0f;
    float height = 2.0f;
};

/**
 * @brief Parameters for capsule primitive
 */
struct CSGCapsuleParams {
    float radius = 1.0f;
    float height = 2.0f;  ///< Total height including caps
};

/**
 * @brief Parameters for cone primitive
 */
struct CSGConeParams {
    float radius = 1.0f;
    float height = 2.0f;
};

/**
 * @brief Parameters for torus primitive
 */
struct CSGTorusParams {
    float majorRadius = 2.0f;  ///< Distance from center to tube center
    float minorRadius = 0.5f;  ///< Radius of the tube
};

/**
 * @brief Parameters for plane primitive
 */
struct CSGPlaneParams {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);  ///< Points into solid
    float distance = 0.0f;  ///< Distance from origin along normal
};

/**
 * @brief Parameters for heightmap primitive
 */
struct CSGHeightmapParams {
    const float* heights = nullptr;  ///< 2D height array (row-major)
    uint32_t width = 0;              ///< Width in samples
    uint32_t depth = 0;              ///< Depth in samples
    float heightScale = 1.0f;        ///< Scale factor for heights
    float baseHeight = 0.0f;         ///< Minimum height
};

/**
 * @brief Parameters for voxel mask primitive
 */
struct CSGVoxelMaskParams {
    const BlockType* blocks = nullptr;  ///< Source block data
    uint32_t sizeX = 0;
    uint32_t sizeY = 0;
    uint32_t sizeZ = 0;
    BlockType solidThreshold = 1;  ///< Blocks >= this are solid
};

// ============================================================================
// CSG Primitive
// ============================================================================

/**
 * @brief CSG primitive with transform and operation
 *
 * A primitive represents a solid shape that can be combined with
 * voxel data using CSG operations.
 */
struct CSGPrimitive {
    /// Primitive type
    CSGPrimitiveType type = CSGPrimitiveType::Box;
    
    /// CSG operation to apply
    CSGOperation operation = CSGOperation::Union;
    
    /// Block type to use for union/replace operations
    BlockType material = BLOCK_STONE;
    
    /// Padding for alignment
    uint8_t padding = 0;
    
    /// World-space position (center of primitive)
    glm::vec3 position = glm::vec3(0.0f);
    
    /// Rotation quaternion
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    /// Scale (for box: half-extents, for others: uniform scale)
    glm::vec3 scale = glm::vec3(1.0f);
    
    /// Type-specific parameters (32 bytes max)
    union {
        CSGSphereParams sphere;
        CSGCylinderParams cylinder;
        CSGCapsuleParams capsule;
        CSGConeParams cone;
        CSGTorusParams torus;
        CSGPlaneParams plane;
        CSGHeightmapParams heightmap;
        CSGVoxelMaskParams voxelMask;
        uint8_t raw[32];
    } params = {};
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    /**
     * @brief Create a box primitive
     */
    static CSGPrimitive box(const glm::vec3& center,
                            const glm::vec3& halfExtents,
                            CSGOperation op = CSGOperation::Union,
                            BlockType mat = BLOCK_STONE) {
        CSGPrimitive p;
        p.type = CSGPrimitiveType::Box;
        p.operation = op;
        p.material = mat;
        p.position = center;
        p.scale = halfExtents;
        return p;
    }
    
    /**
     * @brief Create a sphere primitive
     */
    static CSGPrimitive sphere(const glm::vec3& center,
                               float radius,
                               CSGOperation op = CSGOperation::Union,
                               BlockType mat = BLOCK_STONE) {
        CSGPrimitive p;
        p.type = CSGPrimitiveType::Sphere;
        p.operation = op;
        p.material = mat;
        p.position = center;
        p.params.sphere.radius = radius;
        return p;
    }
    
    /**
     * @brief Create a cylinder primitive
     */
    static CSGPrimitive cylinder(const glm::vec3& center,
                                 float radius,
                                 float height,
                                 CSGOperation op = CSGOperation::Union,
                                 BlockType mat = BLOCK_STONE) {
        CSGPrimitive p;
        p.type = CSGPrimitiveType::Cylinder;
        p.operation = op;
        p.material = mat;
        p.position = center;
        p.params.cylinder.radius = radius;
        p.params.cylinder.height = height;
        return p;
    }
    
    /**
     * @brief Create a capsule primitive
     */
    static CSGPrimitive capsule(const glm::vec3& center,
                                float radius,
                                float height,
                                CSGOperation op = CSGOperation::Union,
                                BlockType mat = BLOCK_STONE) {
        CSGPrimitive p;
        p.type = CSGPrimitiveType::Capsule;
        p.operation = op;
        p.material = mat;
        p.position = center;
        p.params.capsule.radius = radius;
        p.params.capsule.height = height;
        return p;
    }
    
    /**
     * @brief Set rotation using Euler angles (degrees)
     */
    CSGPrimitive& withRotationDegrees(float pitch, float yaw, float roll) {
        glm::vec3 radians(glm::radians(pitch), glm::radians(yaw), glm::radians(roll));
        rotation = glm::quat(radians);
        return *this;
    }
    
    /**
     * @brief Set rotation using quaternion
     */
    CSGPrimitive& withRotation(const glm::quat& q) {
        rotation = q;
        return *this;
    }
    
    // ========================================================================
    // Transform Helpers
    // ========================================================================
    
    /**
     * @brief Get the local-to-world transform matrix
     */
    glm::mat4 getTransform() const {
        glm::mat4 t = glm::mat4(1.0f);
        t = glm::translate(t, position);
        t *= glm::mat4_cast(rotation);
        t = glm::scale(t, scale);
        return t;
    }
    
    /**
     * @brief Get the inverse transform (world-to-local)
     */
    glm::mat4 getInverseTransform() const {
        return glm::inverse(getTransform());
    }
    
    /**
     * @brief Transform a world point to local primitive space
     */
    glm::vec3 worldToLocal(const glm::vec3& worldPoint) const {
        glm::vec3 p = worldPoint - position;
        p = glm::inverse(rotation) * p;
        p = p / scale;
        return p;
    }
};

// ============================================================================
// CSG Node (for CSG trees)
// ============================================================================

/**
 * @brief Node type for CSG tree
 */
enum class CSGNodeType : uint8_t {
    Primitive,      ///< Leaf node with a primitive
    Union,          ///< Union of children
    Difference,     ///< Difference (left - right)
    Intersection,   ///< Intersection of children
};

/**
 * @brief CSG tree node for complex boolean operations
 *
 * Allows building trees of CSG operations for complex shapes:
 * e.g., (Box DIFFERENCE Sphere) UNION Cylinder
 */
struct CSGNode {
    CSGNodeType type = CSGNodeType::Primitive;
    
    /// Primitive data (only valid if type == Primitive)
    CSGPrimitive primitive;
    
    /// Child nodes (only valid if type != Primitive)
    std::unique_ptr<CSGNode> left;
    std::unique_ptr<CSGNode> right;
    
    // ========================================================================
    // Factory Methods
    // ========================================================================
    
    /**
     * @brief Create a primitive leaf node
     */
    static std::unique_ptr<CSGNode> makePrimitive(const CSGPrimitive& prim) {
        auto node = std::make_unique<CSGNode>();
        node->type = CSGNodeType::Primitive;
        node->primitive = prim;
        return node;
    }
    
    /**
     * @brief Create a union node
     */
    static std::unique_ptr<CSGNode> makeUnion(
        std::unique_ptr<CSGNode> a,
        std::unique_ptr<CSGNode> b) {
        auto node = std::make_unique<CSGNode>();
        node->type = CSGNodeType::Union;
        node->left = std::move(a);
        node->right = std::move(b);
        return node;
    }
    
    /**
     * @brief Create a difference node (a - b)
     */
    static std::unique_ptr<CSGNode> makeDifference(
        std::unique_ptr<CSGNode> a,
        std::unique_ptr<CSGNode> b) {
        auto node = std::make_unique<CSGNode>();
        node->type = CSGNodeType::Difference;
        node->left = std::move(a);
        node->right = std::move(b);
        return node;
    }
    
    /**
     * @brief Create an intersection node
     */
    static std::unique_ptr<CSGNode> makeIntersection(
        std::unique_ptr<CSGNode> a,
        std::unique_ptr<CSGNode> b) {
        auto node = std::make_unique<CSGNode>();
        node->type = CSGNodeType::Intersection;
        node->left = std::move(a);
        node->right = std::move(b);
        return node;
    }
    
    /**
     * @brief Check if this is a leaf node
     */
    bool isLeaf() const {
        return type == CSGNodeType::Primitive;
    }
};

// ============================================================================
// CSG Primitive Pool
// ============================================================================

/**
 * @brief Pool of CSG primitives for command queue references
 *
 * Commands reference primitives by index to avoid copying large data.
 */
class CSGPrimitivePool {
public:
    static constexpr size_t MAX_PRIMITIVES = 1024;
    
    /**
     * @brief Add a primitive to the pool
     *
     * @param primitive Primitive to add
     * @return Index of the primitive, or UINT32_MAX if full
     */
    uint32_t add(const CSGPrimitive& primitive) {
        if (primitives_.size() >= MAX_PRIMITIVES) {
            return UINT32_MAX;
        }
        
        uint32_t index = static_cast<uint32_t>(primitives_.size());
        primitives_.push_back(primitive);
        return index;
    }
    
    /**
     * @brief Get a primitive by index
     */
    const CSGPrimitive* get(uint32_t index) const {
        if (index >= primitives_.size()) {
            return nullptr;
        }
        return &primitives_[index];
    }
    
    /**
     * @brief Clear all primitives
     */
    void clear() {
        primitives_.clear();
    }
    
    /**
     * @brief Get number of primitives
     */
    size_t size() const {
        return primitives_.size();
    }
    
private:
    std::vector<CSGPrimitive> primitives_;
};

} // namespace voxel
} // namespace jupiter



