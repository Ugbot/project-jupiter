/**
 * @file primitives.cpp
 * @brief Implementation of procedural mesh generation
 */

#include "rendering/primitives.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace jupiter {
namespace rendering {

// ============================================================================
// Constants
// ============================================================================

static constexpr float PI = 3.14159265358979323846f;
static constexpr float TWO_PI = 2.0f * PI;

// ============================================================================
// Internal Helpers
// ============================================================================

void Primitives::addVertex(MeshData& mesh,
                           float px, float py, float pz,
                           float nx, float ny, float nz,
                           float u, float v,
                           float tx, float ty, float tz, float tw) {
    Vertex3DLit vertex;
    vertex.pos[0] = px;
    vertex.pos[1] = py;
    vertex.pos[2] = pz;
    vertex.normal[0] = nx;
    vertex.normal[1] = ny;
    vertex.normal[2] = nz;
    vertex.texCoord[0] = u;
    vertex.texCoord[1] = v;
    vertex.tangent[0] = tx;
    vertex.tangent[1] = ty;
    vertex.tangent[2] = tz;
    vertex.tangent[3] = tw;
    mesh.vertices.push_back(vertex);
}

void Primitives::addTriangle(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

void Primitives::addQuad(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // Two triangles: a-b-c and a-c-d
    addTriangle(mesh, a, b, c);
    addTriangle(mesh, a, c, d);
}

// ============================================================================
// Cube / Box
// ============================================================================

MeshData Primitives::createCube(float size) {
    return createBox(size, size, size);
}

MeshData Primitives::createBox(float width, float height, float depth) {
    MeshData mesh;
    mesh.reserve(24, 36);  // 6 faces * 4 vertices, 6 faces * 6 indices

    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float hd = depth * 0.5f;

    // Front face (+Z)
    addVertex(mesh, -hw, -hh,  hd,  0, 0, 1,  0, 1);  // 0: bottom-left
    addVertex(mesh,  hw, -hh,  hd,  0, 0, 1,  1, 1);  // 1: bottom-right
    addVertex(mesh,  hw,  hh,  hd,  0, 0, 1,  1, 0);  // 2: top-right
    addVertex(mesh, -hw,  hh,  hd,  0, 0, 1,  0, 0);  // 3: top-left

    // Back face (-Z)
    addVertex(mesh,  hw, -hh, -hd,  0, 0, -1,  0, 1);  // 4
    addVertex(mesh, -hw, -hh, -hd,  0, 0, -1,  1, 1);  // 5
    addVertex(mesh, -hw,  hh, -hd,  0, 0, -1,  1, 0);  // 6
    addVertex(mesh,  hw,  hh, -hd,  0, 0, -1,  0, 0);  // 7

    // Right face (+X)
    addVertex(mesh,  hw, -hh,  hd,  1, 0, 0,  0, 1);  // 8
    addVertex(mesh,  hw, -hh, -hd,  1, 0, 0,  1, 1);  // 9
    addVertex(mesh,  hw,  hh, -hd,  1, 0, 0,  1, 0);  // 10
    addVertex(mesh,  hw,  hh,  hd,  1, 0, 0,  0, 0);  // 11

    // Left face (-X)
    addVertex(mesh, -hw, -hh, -hd, -1, 0, 0,  0, 1);  // 12
    addVertex(mesh, -hw, -hh,  hd, -1, 0, 0,  1, 1);  // 13
    addVertex(mesh, -hw,  hh,  hd, -1, 0, 0,  1, 0);  // 14
    addVertex(mesh, -hw,  hh, -hd, -1, 0, 0,  0, 0);  // 15

    // Top face (+Y)
    addVertex(mesh, -hw,  hh,  hd,  0, 1, 0,  0, 1);  // 16
    addVertex(mesh,  hw,  hh,  hd,  0, 1, 0,  1, 1);  // 17
    addVertex(mesh,  hw,  hh, -hd,  0, 1, 0,  1, 0);  // 18
    addVertex(mesh, -hw,  hh, -hd,  0, 1, 0,  0, 0);  // 19

    // Bottom face (-Y)
    addVertex(mesh, -hw, -hh, -hd,  0, -1, 0,  0, 1);  // 20
    addVertex(mesh,  hw, -hh, -hd,  0, -1, 0,  1, 1);  // 21
    addVertex(mesh,  hw, -hh,  hd,  0, -1, 0,  1, 0);  // 22
    addVertex(mesh, -hw, -hh,  hd,  0, -1, 0,  0, 0);  // 23

    // Generate indices for all 6 faces
    for (int face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        addQuad(mesh, base, base + 1, base + 2, base + 3);
    }

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// UV Sphere
// ============================================================================

MeshData Primitives::createUVSphere(float radius, uint32_t segments, uint32_t rings) {
    MeshData mesh;
    
    // Ensure minimum values
    segments = std::max(3u, segments);
    rings = std::max(2u, rings);

    // Reserve space
    uint32_t vertexCount = (rings + 1) * (segments + 1);
    uint32_t indexCount = rings * segments * 6;
    mesh.reserve(vertexCount, indexCount);

    // Generate vertices
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = PI * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            // Position on unit sphere
            float nx = sinPhi * cosTheta;
            float ny = cosPhi;
            float nz = sinPhi * sinTheta;

            // Position scaled by radius
            float px = radius * nx;
            float py = radius * ny;
            float pz = radius * nz;

            // UV coordinates
            float u = static_cast<float>(seg) / static_cast<float>(segments);
            float v = static_cast<float>(ring) / static_cast<float>(rings);

            addVertex(mesh, px, py, pz, nx, ny, nz, u, v);
        }
    }

    // Generate indices
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            // Two triangles per quad
            addTriangle(mesh, current, next, current + 1);
            addTriangle(mesh, current + 1, next, next + 1);
        }
    }

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// Icosphere
// ============================================================================

uint32_t Primitives::getMiddlePoint(MeshData& mesh,
                                     uint32_t p1, uint32_t p2,
                                     std::vector<std::pair<uint64_t, uint32_t>>& cache,
                                     float radius) {
    // Create a unique key for the edge
    uint64_t smallerIndex = std::min(p1, p2);
    uint64_t greaterIndex = std::max(p1, p2);
    uint64_t key = (smallerIndex << 32) + greaterIndex;

    // Check cache
    for (const auto& entry : cache) {
        if (entry.first == key) {
            return entry.second;
        }
    }

    // Calculate middle point
    const auto& v1 = mesh.vertices[p1];
    const auto& v2 = mesh.vertices[p2];

    float mx = (v1.pos[0] + v2.pos[0]) * 0.5f;
    float my = (v1.pos[1] + v2.pos[1]) * 0.5f;
    float mz = (v1.pos[2] + v2.pos[2]) * 0.5f;

    // Normalize to sphere surface
    float length = std::sqrt(mx*mx + my*my + mz*mz);
    float nx = mx / length;
    float ny = my / length;
    float nz = mz / length;

    // UV from spherical coordinates
    float u = 0.5f + std::atan2(nz, nx) / TWO_PI;
    float v = 0.5f - std::asin(ny) / PI;

    // Add vertex
    uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
    addVertex(mesh, nx * radius, ny * radius, nz * radius, nx, ny, nz, u, v);

    cache.push_back({key, index});
    return index;
}

MeshData Primitives::createIcosphere(float radius, uint32_t subdivisions) {
    MeshData mesh;
    subdivisions = std::min(subdivisions, 5u);  // Limit to prevent explosion

    // Golden ratio
    float t = (1.0f + std::sqrt(5.0f)) * 0.5f;

    // Create initial icosahedron vertices
    float len = std::sqrt(1.0f + t * t);
    float a = 1.0f / len;
    float b = t / len;

    // 12 vertices of icosahedron
    addVertex(mesh, -a,  b,  0, -a,  b,  0, 0, 0);
    addVertex(mesh,  a,  b,  0,  a,  b,  0, 0, 0);
    addVertex(mesh, -a, -b,  0, -a, -b,  0, 0, 0);
    addVertex(mesh,  a, -b,  0,  a, -b,  0, 0, 0);
    addVertex(mesh,  0, -a,  b,  0, -a,  b, 0, 0);
    addVertex(mesh,  0,  a,  b,  0,  a,  b, 0, 0);
    addVertex(mesh,  0, -a, -b,  0, -a, -b, 0, 0);
    addVertex(mesh,  0,  a, -b,  0,  a, -b, 0, 0);
    addVertex(mesh,  b,  0, -a,  b,  0, -a, 0, 0);
    addVertex(mesh,  b,  0,  a,  b,  0,  a, 0, 0);
    addVertex(mesh, -b,  0, -a, -b,  0, -a, 0, 0);
    addVertex(mesh, -b,  0,  a, -b,  0,  a, 0, 0);

    // 20 faces of icosahedron
    std::vector<uint32_t> indices = {
        0, 11, 5,  0, 5, 1,   0, 1, 7,   0, 7, 10,  0, 10, 11,
        1, 5, 9,   5, 11, 4,  11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,   3, 4, 2,   3, 2, 6,   3, 6, 8,   3, 8, 9,
        4, 9, 5,   2, 4, 11,  6, 2, 10,  8, 6, 7,   9, 8, 1
    };

    // Subdivide
    std::vector<std::pair<uint64_t, uint32_t>> cache;
    for (uint32_t i = 0; i < subdivisions; ++i) {
        std::vector<uint32_t> newIndices;
        cache.clear();

        for (size_t j = 0; j < indices.size(); j += 3) {
            uint32_t v1 = indices[j];
            uint32_t v2 = indices[j + 1];
            uint32_t v3 = indices[j + 2];

            uint32_t a = getMiddlePoint(mesh, v1, v2, cache, 1.0f);
            uint32_t b = getMiddlePoint(mesh, v2, v3, cache, 1.0f);
            uint32_t c = getMiddlePoint(mesh, v3, v1, cache, 1.0f);

            newIndices.push_back(v1); newIndices.push_back(a); newIndices.push_back(c);
            newIndices.push_back(v2); newIndices.push_back(b); newIndices.push_back(a);
            newIndices.push_back(v3); newIndices.push_back(c); newIndices.push_back(b);
            newIndices.push_back(a);  newIndices.push_back(b); newIndices.push_back(c);
        }

        indices = newIndices;
    }

    // Scale to desired radius and fix UVs
    for (auto& v : mesh.vertices) {
        float len = std::sqrt(v.pos[0]*v.pos[0] + v.pos[1]*v.pos[1] + v.pos[2]*v.pos[2]);
        v.normal[0] = v.pos[0] / len;
        v.normal[1] = v.pos[1] / len;
        v.normal[2] = v.pos[2] / len;
        v.pos[0] = v.normal[0] * radius;
        v.pos[1] = v.normal[1] * radius;
        v.pos[2] = v.normal[2] * radius;
        
        // Calculate UV from spherical coordinates
        v.texCoord[0] = 0.5f + std::atan2(v.normal[2], v.normal[0]) / TWO_PI;
        v.texCoord[1] = 0.5f - std::asin(v.normal[1]) / PI;
    }

    // Copy indices
    mesh.indices = indices;

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// Cylinder
// ============================================================================

MeshData Primitives::createCylinder(float radius, float height, 
                                     uint32_t segments, uint32_t capSegments) {
    MeshData mesh;
    segments = std::max(3u, segments);

    float halfHeight = height * 0.5f;

    // Side vertices
    for (uint32_t seg = 0; seg <= segments; ++seg) {
        float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);
        float u = static_cast<float>(seg) / static_cast<float>(segments);

        // Bottom vertex
        addVertex(mesh, cosTheta * radius, -halfHeight, sinTheta * radius,
                  cosTheta, 0, sinTheta, u, 1.0f);
        // Top vertex
        addVertex(mesh, cosTheta * radius, halfHeight, sinTheta * radius,
                  cosTheta, 0, sinTheta, u, 0.0f);
    }

    // Side indices
    for (uint32_t seg = 0; seg < segments; ++seg) {
        uint32_t bottom1 = seg * 2;
        uint32_t top1 = seg * 2 + 1;
        uint32_t bottom2 = (seg + 1) * 2;
        uint32_t top2 = (seg + 1) * 2 + 1;

        addTriangle(mesh, bottom1, bottom2, top1);
        addTriangle(mesh, top1, bottom2, top2);
    }

    // Caps
    if (capSegments > 0) {
        uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

        // Top cap center
        addVertex(mesh, 0, halfHeight, 0, 0, 1, 0, 0.5f, 0.5f);
        uint32_t topCenter = baseIndex;

        // Top cap vertices
        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);
            float u = 0.5f + cosTheta * 0.5f;
            float v = 0.5f + sinTheta * 0.5f;
            addVertex(mesh, cosTheta * radius, halfHeight, sinTheta * radius,
                      0, 1, 0, u, v);
        }

        // Top cap indices
        for (uint32_t seg = 0; seg < segments; ++seg) {
            addTriangle(mesh, topCenter, baseIndex + 1 + seg, baseIndex + 2 + seg);
        }

        baseIndex = static_cast<uint32_t>(mesh.vertices.size());

        // Bottom cap center
        addVertex(mesh, 0, -halfHeight, 0, 0, -1, 0, 0.5f, 0.5f);
        uint32_t bottomCenter = baseIndex;

        // Bottom cap vertices
        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);
            float u = 0.5f + cosTheta * 0.5f;
            float v = 0.5f - sinTheta * 0.5f;
            addVertex(mesh, cosTheta * radius, -halfHeight, sinTheta * radius,
                      0, -1, 0, u, v);
        }

        // Bottom cap indices (reversed winding)
        for (uint32_t seg = 0; seg < segments; ++seg) {
            addTriangle(mesh, bottomCenter, baseIndex + 2 + seg, baseIndex + 1 + seg);
        }
    }

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// Cone
// ============================================================================

MeshData Primitives::createCone(float radius, float height,
                                 uint32_t segments, uint32_t capSegments) {
    MeshData mesh;
    segments = std::max(3u, segments);

    float halfHeight = height * 0.5f;

    // Calculate slope for normals
    float slope = radius / height;
    float normalY = 1.0f / std::sqrt(1.0f + slope * slope);
    float normalXZ = slope * normalY;

    // Side vertices (apex + base ring)
    for (uint32_t seg = 0; seg <= segments; ++seg) {
        float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);
        float u = static_cast<float>(seg) / static_cast<float>(segments);

        // Apex vertex (with averaged normal for this segment)
        float midTheta = theta + PI / static_cast<float>(segments);
        float midCos = std::cos(midTheta);
        float midSin = std::sin(midTheta);
        addVertex(mesh, 0, halfHeight, 0,
                  midCos * normalXZ, normalY, midSin * normalXZ, u, 0.0f);

        // Base vertex
        addVertex(mesh, cosTheta * radius, -halfHeight, sinTheta * radius,
                  cosTheta * normalXZ, normalY, sinTheta * normalXZ, u, 1.0f);
    }

    // Side indices
    for (uint32_t seg = 0; seg < segments; ++seg) {
        uint32_t apex1 = seg * 2;
        uint32_t base1 = seg * 2 + 1;
        uint32_t base2 = (seg + 1) * 2 + 1;

        addTriangle(mesh, apex1, base1, base2);
    }

    // Base cap
    if (capSegments > 0) {
        uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

        // Center vertex
        addVertex(mesh, 0, -halfHeight, 0, 0, -1, 0, 0.5f, 0.5f);
        uint32_t center = baseIndex;

        // Base cap vertices
        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);
            float u = 0.5f + cosTheta * 0.5f;
            float v = 0.5f - sinTheta * 0.5f;
            addVertex(mesh, cosTheta * radius, -halfHeight, sinTheta * radius,
                      0, -1, 0, u, v);
        }

        // Base cap indices (reversed winding)
        for (uint32_t seg = 0; seg < segments; ++seg) {
            addTriangle(mesh, center, baseIndex + 2 + seg, baseIndex + 1 + seg);
        }
    }

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// Plane
// ============================================================================

MeshData Primitives::createPlane(float width, float height,
                                  uint32_t segmentsX, uint32_t segmentsZ) {
    MeshData mesh;
    segmentsX = std::max(1u, segmentsX);
    segmentsZ = std::max(1u, segmentsZ);

    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;

    // Generate vertices
    for (uint32_t z = 0; z <= segmentsZ; ++z) {
        float v = static_cast<float>(z) / static_cast<float>(segmentsZ);
        float pz = -halfHeight + height * v;

        for (uint32_t x = 0; x <= segmentsX; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(segmentsX);
            float px = -halfWidth + width * u;

            addVertex(mesh, px, 0, pz, 0, 1, 0, u, v, 1, 0, 0, 1);
        }
    }

    // Generate indices
    for (uint32_t z = 0; z < segmentsZ; ++z) {
        for (uint32_t x = 0; x < segmentsX; ++x) {
            uint32_t topLeft = z * (segmentsX + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = topLeft + segmentsX + 1;
            uint32_t bottomRight = bottomLeft + 1;

            addTriangle(mesh, topLeft, bottomLeft, topRight);
            addTriangle(mesh, topRight, bottomLeft, bottomRight);
        }
    }

    return mesh;
}

// ============================================================================
// Capsule
// ============================================================================

MeshData Primitives::createCapsule(float radius, float height,
                                    uint32_t segments, uint32_t rings) {
    MeshData mesh;
    segments = std::max(3u, segments);
    rings = std::max(2u, rings);

    float cylinderHeight = std::max(0.0f, height - 2.0f * radius);
    float halfCylinderHeight = cylinderHeight * 0.5f;

    // Top hemisphere
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = (PI * 0.5f) * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            float nx = sinPhi * cosTheta;
            float ny = cosPhi;
            float nz = sinPhi * sinTheta;

            float px = radius * nx;
            float py = halfCylinderHeight + radius * ny;
            float pz = radius * nz;

            float u = static_cast<float>(seg) / static_cast<float>(segments);
            float v = static_cast<float>(ring) / static_cast<float>(rings * 2 + 2);

            addVertex(mesh, px, py, pz, nx, ny, nz, u, v);
        }
    }

    // Cylinder section
    for (uint32_t seg = 0; seg <= segments; ++seg) {
        float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);
        float u = static_cast<float>(seg) / static_cast<float>(segments);

        // Top of cylinder
        addVertex(mesh, cosTheta * radius, halfCylinderHeight, sinTheta * radius,
                  cosTheta, 0, sinTheta, u, 0.5f - 0.1f);
        // Bottom of cylinder
        addVertex(mesh, cosTheta * radius, -halfCylinderHeight, sinTheta * radius,
                  cosTheta, 0, sinTheta, u, 0.5f + 0.1f);
    }

    // Bottom hemisphere
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float phi = (PI * 0.5f) + (PI * 0.5f) * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            float nx = sinPhi * cosTheta;
            float ny = cosPhi;
            float nz = sinPhi * sinTheta;

            float px = radius * nx;
            float py = -halfCylinderHeight + radius * ny;
            float pz = radius * nz;

            float u = static_cast<float>(seg) / static_cast<float>(segments);
            float v = 0.5f + 0.5f * static_cast<float>(ring) / static_cast<float>(rings);

            addVertex(mesh, px, py, pz, nx, ny, nz, u, v);
        }
    }

    // Generate indices for top hemisphere
    uint32_t hemisphereVerts = (rings + 1) * (segments + 1);
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;
            addTriangle(mesh, current, next, current + 1);
            addTriangle(mesh, current + 1, next, next + 1);
        }
    }

    // Cylinder indices
    uint32_t cylStart = hemisphereVerts;
    for (uint32_t seg = 0; seg < segments; ++seg) {
        uint32_t top1 = cylStart + seg * 2;
        uint32_t bot1 = top1 + 1;
        uint32_t top2 = cylStart + (seg + 1) * 2;
        uint32_t bot2 = top2 + 1;
        addTriangle(mesh, top1, bot1, top2);
        addTriangle(mesh, top2, bot1, bot2);
    }

    // Bottom hemisphere indices
    uint32_t botStart = cylStart + (segments + 1) * 2;
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t current = botStart + ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;
            addTriangle(mesh, current, next, current + 1);
            addTriangle(mesh, current + 1, next, next + 1);
        }
    }

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// Torus
// ============================================================================

MeshData Primitives::createTorus(float majorRadius, float minorRadius,
                                  uint32_t majorSegments, uint32_t minorSegments) {
    MeshData mesh;
    majorSegments = std::max(3u, majorSegments);
    minorSegments = std::max(3u, minorSegments);

    for (uint32_t i = 0; i <= majorSegments; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(majorSegments);
        float theta = u * TWO_PI;
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        for (uint32_t j = 0; j <= minorSegments; ++j) {
            float v = static_cast<float>(j) / static_cast<float>(minorSegments);
            float phi = v * TWO_PI;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            // Position on torus
            float px = (majorRadius + minorRadius * cosPhi) * cosTheta;
            float py = minorRadius * sinPhi;
            float pz = (majorRadius + minorRadius * cosPhi) * sinTheta;

            // Normal (pointing outward from tube center)
            float nx = cosPhi * cosTheta;
            float ny = sinPhi;
            float nz = cosPhi * sinTheta;

            addVertex(mesh, px, py, pz, nx, ny, nz, u, v);
        }
    }

    // Generate indices
    for (uint32_t i = 0; i < majorSegments; ++i) {
        for (uint32_t j = 0; j < minorSegments; ++j) {
            uint32_t current = i * (minorSegments + 1) + j;
            uint32_t next = current + minorSegments + 1;

            addTriangle(mesh, current, next, current + 1);
            addTriangle(mesh, current + 1, next, next + 1);
        }
    }

    recalculateTangents(mesh);
    return mesh;
}

// ============================================================================
// Utility Functions
// ============================================================================

AABB Primitives::calculateBounds(const MeshData& mesh) {
    AABB bounds;
    if (mesh.vertices.empty()) return bounds;

    bounds.min[0] = bounds.max[0] = mesh.vertices[0].pos[0];
    bounds.min[1] = bounds.max[1] = mesh.vertices[0].pos[1];
    bounds.min[2] = bounds.max[2] = mesh.vertices[0].pos[2];

    for (const auto& v : mesh.vertices) {
        bounds.expand(v.pos[0], v.pos[1], v.pos[2]);
    }

    return bounds;
}

void Primitives::recalculateNormals(MeshData& mesh) {
    // Reset normals
    for (auto& v : mesh.vertices) {
        v.normal[0] = v.normal[1] = v.normal[2] = 0.0f;
    }

    // Accumulate face normals
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        auto& v0 = mesh.vertices[i0];
        auto& v1 = mesh.vertices[i1];
        auto& v2 = mesh.vertices[i2];

        // Edge vectors
        float e1x = v1.pos[0] - v0.pos[0];
        float e1y = v1.pos[1] - v0.pos[1];
        float e1z = v1.pos[2] - v0.pos[2];
        float e2x = v2.pos[0] - v0.pos[0];
        float e2y = v2.pos[1] - v0.pos[1];
        float e2z = v2.pos[2] - v0.pos[2];

        // Cross product
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;

        // Accumulate
        v0.normal[0] += nx; v0.normal[1] += ny; v0.normal[2] += nz;
        v1.normal[0] += nx; v1.normal[1] += ny; v1.normal[2] += nz;
        v2.normal[0] += nx; v2.normal[1] += ny; v2.normal[2] += nz;
    }

    // Normalize
    for (auto& v : mesh.vertices) {
        float len = std::sqrt(v.normal[0]*v.normal[0] + 
                              v.normal[1]*v.normal[1] + 
                              v.normal[2]*v.normal[2]);
        if (len > 0.0001f) {
            v.normal[0] /= len;
            v.normal[1] /= len;
            v.normal[2] /= len;
        }
    }
}

void Primitives::recalculateTangents(MeshData& mesh) {
    // Initialize tangents
    for (auto& v : mesh.vertices) {
        v.tangent[0] = v.tangent[1] = v.tangent[2] = 0.0f;
        v.tangent[3] = 1.0f;
    }

    std::vector<float> bitangents(mesh.vertices.size() * 3, 0.0f);

    // Calculate tangents per triangle
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        auto& v0 = mesh.vertices[i0];
        auto& v1 = mesh.vertices[i1];
        auto& v2 = mesh.vertices[i2];

        float x1 = v1.pos[0] - v0.pos[0];
        float x2 = v2.pos[0] - v0.pos[0];
        float y1 = v1.pos[1] - v0.pos[1];
        float y2 = v2.pos[1] - v0.pos[1];
        float z1 = v1.pos[2] - v0.pos[2];
        float z2 = v2.pos[2] - v0.pos[2];

        float s1 = v1.texCoord[0] - v0.texCoord[0];
        float s2 = v2.texCoord[0] - v0.texCoord[0];
        float t1 = v1.texCoord[1] - v0.texCoord[1];
        float t2 = v2.texCoord[1] - v0.texCoord[1];

        float r = 1.0f / (s1 * t2 - s2 * t1 + 0.0001f);

        float tx = (t2 * x1 - t1 * x2) * r;
        float ty = (t2 * y1 - t1 * y2) * r;
        float tz = (t2 * z1 - t1 * z2) * r;

        float bx = (s1 * x2 - s2 * x1) * r;
        float by = (s1 * y2 - s2 * y1) * r;
        float bz = (s1 * z2 - s2 * z1) * r;

        v0.tangent[0] += tx; v0.tangent[1] += ty; v0.tangent[2] += tz;
        v1.tangent[0] += tx; v1.tangent[1] += ty; v1.tangent[2] += tz;
        v2.tangent[0] += tx; v2.tangent[1] += ty; v2.tangent[2] += tz;

        bitangents[i0 * 3] += bx; bitangents[i0 * 3 + 1] += by; bitangents[i0 * 3 + 2] += bz;
        bitangents[i1 * 3] += bx; bitangents[i1 * 3 + 1] += by; bitangents[i1 * 3 + 2] += bz;
        bitangents[i2 * 3] += bx; bitangents[i2 * 3 + 1] += by; bitangents[i2 * 3 + 2] += bz;
    }

    // Orthonormalize and calculate handedness
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& v = mesh.vertices[i];
        float* b = &bitangents[i * 3];

        // Gram-Schmidt orthonormalize
        float dot = v.normal[0] * v.tangent[0] + v.normal[1] * v.tangent[1] + v.normal[2] * v.tangent[2];
        v.tangent[0] -= v.normal[0] * dot;
        v.tangent[1] -= v.normal[1] * dot;
        v.tangent[2] -= v.normal[2] * dot;

        float len = std::sqrt(v.tangent[0]*v.tangent[0] + v.tangent[1]*v.tangent[1] + v.tangent[2]*v.tangent[2]);
        if (len > 0.0001f) {
            v.tangent[0] /= len;
            v.tangent[1] /= len;
            v.tangent[2] /= len;
        } else {
            // Fallback tangent
            v.tangent[0] = 1.0f;
            v.tangent[1] = 0.0f;
            v.tangent[2] = 0.0f;
        }

        // Calculate handedness
        float cx = v.normal[1] * v.tangent[2] - v.normal[2] * v.tangent[1];
        float cy = v.normal[2] * v.tangent[0] - v.normal[0] * v.tangent[2];
        float cz = v.normal[0] * v.tangent[1] - v.normal[1] * v.tangent[0];
        float dotB = cx * b[0] + cy * b[1] + cz * b[2];
        v.tangent[3] = (dotB < 0.0f) ? -1.0f : 1.0f;
    }
}

void Primitives::flipNormals(MeshData& mesh) {
    for (auto& v : mesh.vertices) {
        v.normal[0] = -v.normal[0];
        v.normal[1] = -v.normal[1];
        v.normal[2] = -v.normal[2];
    }
    
    // Reverse winding order
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
    }
}

void Primitives::scale(MeshData& mesh, float factor) {
    scale(mesh, factor, factor, factor);
}

void Primitives::scale(MeshData& mesh, float x, float y, float z) {
    for (auto& v : mesh.vertices) {
        v.pos[0] *= x;
        v.pos[1] *= y;
        v.pos[2] *= z;
    }
}

void Primitives::translate(MeshData& mesh, float x, float y, float z) {
    for (auto& v : mesh.vertices) {
        v.pos[0] += x;
        v.pos[1] += y;
        v.pos[2] += z;
    }
}

MeshData Primitives::merge(const std::vector<MeshData>& meshes) {
    MeshData result;
    
    size_t totalVerts = 0;
    size_t totalIndices = 0;
    for (const auto& m : meshes) {
        totalVerts += m.vertices.size();
        totalIndices += m.indices.size();
    }
    
    result.reserve(totalVerts, totalIndices);

    for (const auto& m : meshes) {
        uint32_t baseIndex = static_cast<uint32_t>(result.vertices.size());
        
        // Copy vertices
        result.vertices.insert(result.vertices.end(), m.vertices.begin(), m.vertices.end());
        
        // Copy indices with offset
        for (uint32_t idx : m.indices) {
            result.indices.push_back(baseIndex + idx);
        }
    }

    return result;
}

} // namespace rendering
} // namespace jupiter

