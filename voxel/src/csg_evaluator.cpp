/**
 * @file csg_evaluator.cpp
 * @brief Implementation of CSG evaluator
 */

#include <voxel/csg_evaluator.h>
#include <voxel/voxel_column.h>
#include <cmath>
#include <algorithm>

namespace jupiter {
namespace voxel {

// ============================================================================
// Primitive Evaluation
// ============================================================================

void CSGEvaluator::evaluate(const CSGPrimitive& primitive,
                            BlockType* blocks,
                            const ChunkCoord& chunkCoord) {
    if (!blocks) return;
    
    // World offset for this chunk
    const float worldX = static_cast<float>(chunkCoord.x * CHUNK_SIZE);
    const float worldY = static_cast<float>(chunkCoord.y * CHUNK_SIZE);
    const float worldZ = static_cast<float>(chunkCoord.z * CHUNK_SIZE);
    
    // Iterate all voxels in the chunk
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                // World position of voxel center
                glm::vec3 worldPoint(
                    worldX + static_cast<float>(lx) + 0.5f,
                    worldY + static_cast<float>(ly) + 0.5f,
                    worldZ + static_cast<float>(lz) + 0.5f
                );
                
                // Evaluate SDF at this point
                float sdf = evaluatePrimitiveSDF(primitive, worldPoint);
                
                // Get block index (using padded layout)
                int idx = ChunkVoxelData::getIndex(lx, ly, lz);
                
                // Apply CSG operation
                applyOperation(primitive.operation, primitive.material,
                              blocks[idx], sdf);
            }
        }
    }
}

void CSGEvaluator::evaluate(const CSGPrimitive& primitive,
                            ChunkColumns& chunk,
                            const ChunkCoord& chunkCoord) {
    // World offset for this chunk
    const float worldX = static_cast<float>(chunkCoord.x * CHUNK_SIZE);
    const float worldY = static_cast<float>(chunkCoord.y * CHUNK_SIZE);
    const float worldZ = static_cast<float>(chunkCoord.z * CHUNK_SIZE);
    
    // Iterate all voxels in columnar order (better cache locality)
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            VoxelColumn& col = chunk.at(x, z);
            
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                // World position of voxel center
                glm::vec3 worldPoint(
                    worldX + static_cast<float>(x) + 0.5f,
                    worldY + static_cast<float>(y) + 0.5f,
                    worldZ + static_cast<float>(z) + 0.5f
                );
                
                // Evaluate SDF at this point
                float sdf = evaluatePrimitiveSDF(primitive, worldPoint);
                
                // Apply CSG operation directly to column
                BlockType& block = col.blocks[y];
                applyOperation(primitive.operation, primitive.material, block, sdf);
            }
            
            // Update column bounds after modification
            col.updateBounds();
        }
    }
    
    // Increment edit generation
    chunk.incrementGeneration();
}

void CSGEvaluator::evaluateTree(const CSGNode* root,
                                BlockType* blocks,
                                const ChunkCoord& chunkCoord) {
    if (!root || !blocks) return;
    
    // World offset for this chunk
    const float worldX = static_cast<float>(chunkCoord.x * CHUNK_SIZE);
    const float worldY = static_cast<float>(chunkCoord.y * CHUNK_SIZE);
    const float worldZ = static_cast<float>(chunkCoord.z * CHUNK_SIZE);
    
    // For tree evaluation, we need the material from the root
    // For boolean ops, use the left child's material
    BlockType material = BLOCK_STONE;
    if (root->isLeaf()) {
        material = root->primitive.material;
    } else if (root->left && root->left->isLeaf()) {
        material = root->left->primitive.material;
    }
    
    // Iterate all voxels in the chunk
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                glm::vec3 worldPoint(
                    worldX + static_cast<float>(lx) + 0.5f,
                    worldY + static_cast<float>(ly) + 0.5f,
                    worldZ + static_cast<float>(lz) + 0.5f
                );
                
                float sdf = evaluateNodeSDF(root, worldPoint);
                
                int idx = ChunkVoxelData::getIndex(lx, ly, lz);
                
                // For tree, apply based on the root's type
                CSGOperation op = CSGOperation::Union;
                if (!root->isLeaf()) {
                    switch (root->type) {
                        case CSGNodeType::Union:
                            op = CSGOperation::Union;
                            break;
                        case CSGNodeType::Difference:
                            op = CSGOperation::Difference;
                            break;
                        case CSGNodeType::Intersection:
                            op = CSGOperation::Intersection;
                            break;
                        default:
                            break;
                    }
                } else {
                    op = root->primitive.operation;
                }
                
                applyOperation(op, material, blocks[idx], sdf);
            }
        }
    }
}

// ============================================================================
// Signed Distance Functions
// ============================================================================

float CSGEvaluator::sdfBox(const glm::vec3& p, const glm::vec3& halfExtents) {
    glm::vec3 q = glm::abs(p) - halfExtents;
    return glm::length(glm::max(q, glm::vec3(0.0f))) +
           glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
}

float CSGEvaluator::sdfSphere(const glm::vec3& p, float radius) {
    return glm::length(p) - radius;
}

float CSGEvaluator::sdfCylinder(const glm::vec3& p, float radius, float height) {
    float halfHeight = height * 0.5f;
    glm::vec2 d = glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y)) -
                  glm::vec2(radius, halfHeight);
    return glm::min(glm::max(d.x, d.y), 0.0f) +
           glm::length(glm::max(d, glm::vec2(0.0f)));
}

float CSGEvaluator::sdfCapsule(const glm::vec3& p, float radius, float height) {
    float halfHeight = (height - 2.0f * radius) * 0.5f;
    halfHeight = glm::max(halfHeight, 0.0f);
    
    glm::vec3 pa = p - glm::vec3(0.0f, -halfHeight, 0.0f);
    glm::vec3 pb = p - glm::vec3(0.0f, halfHeight, 0.0f);
    glm::vec3 ba = glm::vec3(0.0f, 2.0f * halfHeight, 0.0f);
    
    float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
    return glm::length(pa - ba * h) - radius;
}

float CSGEvaluator::sdfCone(const glm::vec3& p, float radius, float height) {
    // Cone with tip at (0, height, 0) and base at y=0
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y);
    
    glm::vec2 tip = glm::vec2(0.0f, height);
    glm::vec2 base = glm::vec2(radius, 0.0f);
    
    glm::vec2 e = base - tip;
    glm::vec2 w = q - tip;
    
    float t = glm::clamp(glm::dot(w, e) / glm::dot(e, e), 0.0f, 1.0f);
    glm::vec2 closest = tip + e * t;
    
    return glm::length(q - closest) * glm::sign(q.x * e.y - q.y * e.x + tip.x * base.y);
}

float CSGEvaluator::sdfTorus(const glm::vec3& p, float majorRadius, float minorRadius) {
    glm::vec2 q = glm::vec2(glm::length(glm::vec2(p.x, p.z)) - majorRadius, p.y);
    return glm::length(q) - minorRadius;
}

float CSGEvaluator::sdfPlane(const glm::vec3& p, const glm::vec3& normal, float distance) {
    return glm::dot(p, normal) - distance;
}

// ============================================================================
// Internal Methods
// ============================================================================

float CSGEvaluator::evaluatePrimitiveSDF(const CSGPrimitive& primitive,
                                         const glm::vec3& worldPoint) const {
    // Transform world point to local primitive space
    glm::vec3 localPoint = primitive.worldToLocal(worldPoint);
    
    switch (primitive.type) {
        case CSGPrimitiveType::Box:
            // For box, scale IS the half-extents, already applied in worldToLocal
            return sdfBox(localPoint, glm::vec3(1.0f));
            
        case CSGPrimitiveType::Sphere:
            return sdfSphere(localPoint, primitive.params.sphere.radius / primitive.scale.x);
            
        case CSGPrimitiveType::Cylinder:
            return sdfCylinder(localPoint,
                              primitive.params.cylinder.radius / primitive.scale.x,
                              primitive.params.cylinder.height / primitive.scale.y);
            
        case CSGPrimitiveType::Capsule:
            return sdfCapsule(localPoint,
                             primitive.params.capsule.radius / primitive.scale.x,
                             primitive.params.capsule.height / primitive.scale.y);
            
        case CSGPrimitiveType::Cone:
            return sdfCone(localPoint,
                          primitive.params.cone.radius / primitive.scale.x,
                          primitive.params.cone.height / primitive.scale.y);
            
        case CSGPrimitiveType::Torus:
            return sdfTorus(localPoint,
                           primitive.params.torus.majorRadius / primitive.scale.x,
                           primitive.params.torus.minorRadius / primitive.scale.x);
            
        case CSGPrimitiveType::Plane:
            // Plane is evaluated in world space
            return sdfPlane(worldPoint,
                           primitive.params.plane.normal,
                           primitive.params.plane.distance);
            
        case CSGPrimitiveType::Heightmap:
            // Heightmap needs special handling
            {
                const auto& hm = primitive.params.heightmap;
                if (!hm.heights || hm.width == 0 || hm.depth == 0) {
                    return 1000.0f;  // Invalid heightmap = outside
                }
                
                // Sample height at this XZ position
                float u = (worldPoint.x - primitive.position.x) / primitive.scale.x + 0.5f;
                float v = (worldPoint.z - primitive.position.z) / primitive.scale.z + 0.5f;
                
                if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
                    return 1000.0f;  // Outside heightmap bounds
                }
                
                // Bilinear sample
                float fx = u * (hm.width - 1);
                float fz = v * (hm.depth - 1);
                int x0 = static_cast<int>(fx);
                int z0 = static_cast<int>(fz);
                int x1 = std::min(x0 + 1, static_cast<int>(hm.width) - 1);
                int z1 = std::min(z0 + 1, static_cast<int>(hm.depth) - 1);
                float tx = fx - x0;
                float tz = fz - z0;
                
                float h00 = hm.heights[z0 * hm.width + x0];
                float h10 = hm.heights[z0 * hm.width + x1];
                float h01 = hm.heights[z1 * hm.width + x0];
                float h11 = hm.heights[z1 * hm.width + x1];
                
                float height = glm::mix(
                    glm::mix(h00, h10, tx),
                    glm::mix(h01, h11, tx),
                    tz
                );
                
                height = hm.baseHeight + height * hm.heightScale;
                
                return worldPoint.y - height;
            }
            
        case CSGPrimitiveType::VoxelMask:
            // Voxel mask needs special handling
            {
                const auto& vm = primitive.params.voxelMask;
                if (!vm.blocks || vm.sizeX == 0 || vm.sizeY == 0 || vm.sizeZ == 0) {
                    return 1000.0f;
                }
                
                // Calculate index into mask
                int x = static_cast<int>(localPoint.x + 0.5f);
                int y = static_cast<int>(localPoint.y + 0.5f);
                int z = static_cast<int>(localPoint.z + 0.5f);
                
                if (x < 0 || x >= static_cast<int>(vm.sizeX) ||
                    y < 0 || y >= static_cast<int>(vm.sizeY) ||
                    z < 0 || z >= static_cast<int>(vm.sizeZ)) {
                    return 1000.0f;
                }
                
                int idx = z + y * vm.sizeZ + x * vm.sizeY * vm.sizeZ;
                return vm.blocks[idx] >= vm.solidThreshold ? -0.5f : 0.5f;
            }
            
        default:
            return 1000.0f;  // Unknown type = outside
    }
}

float CSGEvaluator::evaluateNodeSDF(const CSGNode* node,
                                    const glm::vec3& worldPoint) const {
    if (!node) return 1000.0f;
    
    if (node->isLeaf()) {
        return evaluatePrimitiveSDF(node->primitive, worldPoint);
    }
    
    float leftSDF = node->left ? evaluateNodeSDF(node->left.get(), worldPoint) : 1000.0f;
    float rightSDF = node->right ? evaluateNodeSDF(node->right.get(), worldPoint) : 1000.0f;
    
    switch (node->type) {
        case CSGNodeType::Union:
            if (smoothFactor_ > 0.0f) {
                return sdfSmoothUnion(leftSDF, rightSDF, smoothFactor_);
            }
            return sdfUnion(leftSDF, rightSDF);
            
        case CSGNodeType::Difference:
            if (smoothFactor_ > 0.0f) {
                return sdfSmoothDifference(leftSDF, rightSDF, smoothFactor_);
            }
            return sdfDifference(leftSDF, rightSDF);
            
        case CSGNodeType::Intersection:
            return sdfIntersection(leftSDF, rightSDF);
            
        default:
            return leftSDF;
    }
}

void CSGEvaluator::applyOperation(CSGOperation op,
                                  BlockType material,
                                  BlockType& existing,
                                  float sdfValue) {
    bool primitiveIsSolid = sdfValue <= surfaceThreshold_;
    bool existingIsSolid = existing != BLOCK_AIR;
    
    switch (op) {
        case CSGOperation::Union:
            // Add material where primitive is solid
            if (primitiveIsSolid) {
                existing = material;
            }
            break;
            
        case CSGOperation::Difference:
            // Remove material where primitive is solid
            if (primitiveIsSolid && existingIsSolid) {
                existing = BLOCK_AIR;
            }
            break;
            
        case CSGOperation::Intersection:
            // Keep only where both are solid
            if (!primitiveIsSolid && existingIsSolid) {
                existing = BLOCK_AIR;
            }
            break;
            
        case CSGOperation::Replace:
            // Unconditionally set material where primitive is solid
            if (primitiveIsSolid) {
                existing = material;
            }
            break;
    }
}

} // namespace voxel
} // namespace jupiter

