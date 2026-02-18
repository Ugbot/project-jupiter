/**
 * @file primitives.h
 * @brief Primitive shape generators for testing and prototyping
 * 
 * Provides simple geometric primitives with proper vertex data:
 * - Cube (unit, with normals and UVs)
 * - Sphere (UV sphere, configurable detail)
 * - Plane (quad or subdivided grid)
 * - Triangle (basic test geometry)
 */

#pragma once

#include "rendering/ghi/ghi.h"
#include <glm/glm.hpp>
#include <vector>

namespace jupiter {
namespace rendering {
namespace primitives {

/**
 * @brief Standard vertex format for all primitives
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    
    Vertex() = default;
    Vertex(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv)
        : position(p), normal(n), texCoord(uv) {}
};

/**
 * @brief Mesh data (vertices + indices)
 */
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    
    // Create GHI buffers from this mesh
    ghi::BufferHandle createVertexBuffer() const;
    ghi::BufferHandle createIndexBuffer() const;
};

/**
 * @brief Generate a unit cube (1x1x1) centered at origin
 * 
 * @return MeshData with 24 vertices (4 per face) and 36 indices
 */
MeshData createCube();

/**
 * @brief Generate a UV sphere
 * 
 * @param radius Sphere radius
 * @param segments Horizontal segments (longitude)
 * @param rings Vertical segments (latitude)
 * @return MeshData with sphere geometry
 */
MeshData createSphere(float radius = 1.0f, int segments = 32, int rings = 16);

/**
 * @brief Generate a plane (quad or subdivided grid)
 * 
 * @param width Width in world units
 * @param height Height in world units  
 * @param subdivisions Number of subdivisions (1 = single quad)
 * @return MeshData with plane geometry
 */
MeshData createPlane(float width = 1.0f, float height = 1.0f, int subdivisions = 1);

/**
 * @brief Generate a simple triangle (for testing)
 * 
 * @return MeshData with 3 vertices, no indices
 */
MeshData createTriangle();

} // namespace primitives
} // namespace rendering
} // namespace jupiter
