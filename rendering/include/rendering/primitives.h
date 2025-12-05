#pragma once

/**
 * @file primitives.h
 * @brief Procedural mesh generation for basic geometric shapes
 * 
 * Provides utilities to generate vertices and indices for common shapes:
 * - Cube/Box
 * - Sphere (UV sphere and icosphere)
 * - Cylinder
 * - Cone
 * - Plane/Quad
 * - Capsule
 * - Torus
 * 
 * All shapes are generated with:
 * - Proper normals for lighting
 * - UV coordinates for texturing
 * - Tangents for normal mapping
 * 
 * Usage:
 *   MeshData cube = Primitives::createCube(1.0f);
 *   MeshData sphere = Primitives::createUVSphere(1.0f, 32, 16);
 *   
 *   // Create GPU buffers from mesh data
 *   VulkanMesh gpuMesh;
 *   gpuMesh.create(device, allocator, cube.vertices, cube.indices);
 */

#include "rendering/vertex_formats.h"
#include <vector>
#include <cstdint>
#include <cmath>

namespace jupiter {
namespace rendering {

/**
 * @brief Container for generated mesh data
 */
struct MeshData {
    std::vector<Vertex3DLit> vertices;
    std::vector<uint32_t> indices;
    
    void clear() {
        vertices.clear();
        indices.clear();
    }
    
    void reserve(size_t vertexCount, size_t indexCount) {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    }
};

/**
 * @brief Axis-aligned bounding box
 */
struct AABB {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    
    void expand(float x, float y, float z) {
        if (x < min[0]) min[0] = x;
        if (y < min[1]) min[1] = y;
        if (z < min[2]) min[2] = z;
        if (x > max[0]) max[0] = x;
        if (y > max[1]) max[1] = y;
        if (z > max[2]) max[2] = z;
    }
};

/**
 * @brief Static utility class for generating primitive meshes
 */
class Primitives {
public:
    // ========================================================================
    // Basic Shapes
    // ========================================================================

    /**
     * @brief Create a unit cube centered at origin
     * @param size Edge length (default 1.0)
     * @return MeshData with vertices and indices
     */
    static MeshData createCube(float size = 1.0f);

    /**
     * @brief Create a box with independent dimensions
     * @param width X dimension
     * @param height Y dimension
     * @param depth Z dimension
     * @return MeshData with vertices and indices
     */
    static MeshData createBox(float width, float height, float depth);

    /**
     * @brief Create a UV sphere (latitude/longitude grid)
     * @param radius Sphere radius
     * @param segments Horizontal divisions (around equator)
     * @param rings Vertical divisions (pole to pole)
     * @return MeshData with vertices and indices
     * 
     * UV spheres have more uniform UVs but pinch at poles.
     */
    static MeshData createUVSphere(float radius = 1.0f, 
                                   uint32_t segments = 32, 
                                   uint32_t rings = 16);

    /**
     * @brief Create an icosphere (subdivided icosahedron)
     * @param radius Sphere radius
     * @param subdivisions Number of subdivision iterations (0-5 recommended)
     * @return MeshData with vertices and indices
     * 
     * Icospheres have more uniform triangle distribution but complex UVs.
     */
    static MeshData createIcosphere(float radius = 1.0f, 
                                    uint32_t subdivisions = 2);

    /**
     * @brief Create a cylinder
     * @param radius Cylinder radius
     * @param height Cylinder height
     * @param segments Number of radial segments
     * @param capSegments Segments for end caps (0 = no caps)
     * @return MeshData with vertices and indices
     */
    static MeshData createCylinder(float radius = 0.5f, 
                                   float height = 1.0f,
                                   uint32_t segments = 32,
                                   uint32_t capSegments = 1);

    /**
     * @brief Create a cone
     * @param radius Base radius
     * @param height Cone height
     * @param segments Number of radial segments
     * @param capSegments Segments for base cap (0 = no cap)
     * @return MeshData with vertices and indices
     */
    static MeshData createCone(float radius = 0.5f,
                               float height = 1.0f,
                               uint32_t segments = 32,
                               uint32_t capSegments = 1);

    /**
     * @brief Create a flat plane/quad
     * @param width X dimension
     * @param height Z dimension (plane lies in XZ plane)
     * @param segmentsX X subdivisions
     * @param segmentsZ Z subdivisions
     * @return MeshData with vertices and indices
     */
    static MeshData createPlane(float width = 1.0f,
                                float height = 1.0f,
                                uint32_t segmentsX = 1,
                                uint32_t segmentsZ = 1);

    /**
     * @brief Create a capsule (cylinder with hemisphere caps)
     * @param radius Capsule radius
     * @param height Total height (including caps)
     * @param segments Radial segments
     * @param rings Ring segments for caps
     * @return MeshData with vertices and indices
     */
    static MeshData createCapsule(float radius = 0.5f,
                                  float height = 2.0f,
                                  uint32_t segments = 32,
                                  uint32_t rings = 8);

    /**
     * @brief Create a torus (donut shape)
     * @param majorRadius Distance from center to tube center
     * @param minorRadius Tube radius
     * @param majorSegments Segments around the ring
     * @param minorSegments Segments around the tube
     * @return MeshData with vertices and indices
     */
    static MeshData createTorus(float majorRadius = 1.0f,
                                float minorRadius = 0.3f,
                                uint32_t majorSegments = 32,
                                uint32_t minorSegments = 16);

    // ========================================================================
    // Utility Functions
    // ========================================================================

    /**
     * @brief Calculate AABB for mesh data
     */
    static AABB calculateBounds(const MeshData& mesh);

    /**
     * @brief Recalculate normals for mesh (flat shading)
     */
    static void recalculateNormals(MeshData& mesh);

    /**
     * @brief Recalculate tangents for mesh
     */
    static void recalculateTangents(MeshData& mesh);

    /**
     * @brief Flip normals (for inside-out rendering)
     */
    static void flipNormals(MeshData& mesh);

    /**
     * @brief Scale mesh uniformly
     */
    static void scale(MeshData& mesh, float factor);

    /**
     * @brief Scale mesh non-uniformly
     */
    static void scale(MeshData& mesh, float x, float y, float z);

    /**
     * @brief Translate mesh
     */
    static void translate(MeshData& mesh, float x, float y, float z);

    /**
     * @brief Merge multiple meshes into one
     */
    static MeshData merge(const std::vector<MeshData>& meshes);

private:
    // Internal helpers
    static void addVertex(MeshData& mesh, 
                         float px, float py, float pz,
                         float nx, float ny, float nz,
                         float u, float v,
                         float tx = 1.0f, float ty = 0.0f, float tz = 0.0f, float tw = 1.0f);
    
    static void addTriangle(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c);
    static void addQuad(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    
    // Icosphere helpers
    static uint32_t getMiddlePoint(MeshData& mesh, 
                                   uint32_t p1, uint32_t p2,
                                   std::vector<std::pair<uint64_t, uint32_t>>& cache,
                                   float radius);
};

} // namespace rendering
} // namespace jupiter

