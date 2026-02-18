/**
 * @file vertex_encoder.cpp
 * @brief Implementation of voxel vertex encoding
 */

#include <voxel/vertex_encoder.h>

namespace jupiter {
namespace voxel {

// ============================================================================
// Vertex Position Offsets per Face
// ============================================================================

/**
 * Vertex order for each face in UV space:
 *   3---2
 *   |   |
 *   0---1
 *
 * Arrays are [vertex][xyz] relative to block origin
 */

// +X face (X=1)
static constexpr uint8_t FACE_POS_X_VERTS[4][3] = {
    {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}
};

// -X face (X=0)
static constexpr uint8_t FACE_NEG_X_VERTS[4][3] = {
    {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}
};

// +Y face (Y=1, top)
static constexpr uint8_t FACE_POS_Y_VERTS[4][3] = {
    {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}
};

// -Y face (Y=0, bottom)
static constexpr uint8_t FACE_NEG_Y_VERTS[4][3] = {
    {0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}
};

// +Z face (Z=1)
static constexpr uint8_t FACE_POS_Z_VERTS[4][3] = {
    {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}
};

// -Z face (Z=0)
static constexpr uint8_t FACE_NEG_Z_VERTS[4][3] = {
    {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}
};

static const uint8_t (*FACE_VERTS[6])[3] = {
    FACE_POS_X_VERTS,
    FACE_NEG_X_VERTS,
    FACE_POS_Y_VERTS,
    FACE_NEG_Y_VERTS,
    FACE_POS_Z_VERTS,
    FACE_NEG_Z_VERTS,
};

// UV scale factors for greedy meshed quads
// For each face: which axis is U, which is V
static constexpr int FACE_UV_AXES[6][2] = {
    {2, 1},  // +X: U=Z, V=Y
    {2, 1},  // -X: U=Z, V=Y
    {0, 2},  // +Y: U=X, V=Z
    {0, 2},  // -Y: U=X, V=Z
    {0, 1},  // +Z: U=X, V=Y
    {0, 1},  // -Z: U=X, V=Y
};

// ============================================================================
// VertexEncoder Implementation
// ============================================================================

void VertexEncoder::encode(const std::vector<MergedQuad>& quads,
                           const ChunkCoord& chunkCoord) {
    vertices_.clear();
    vertices_.reserve(quads.size() * 4);
    quadCount_ = quads.size();
    
    for (const auto& quad : quads) {
        encodeQuad(quad, chunkCoord);
    }
}

void VertexEncoder::encodeTriangles(const std::vector<MergedQuad>& quads,
                                    const ChunkCoord& chunkCoord) {
    vertices_.clear();
    vertices_.reserve(quads.size() * 6);
    quadCount_ = quads.size();
    
    for (const auto& quad : quads) {
        // Get the 4 vertices
        size_t baseIdx = vertices_.size();
        encodeQuad(quad, chunkCoord);
        
        // Duplicate to make 2 triangles (6 vertices)
        VoxelVertexGPU v0 = vertices_[baseIdx + 0];
        VoxelVertexGPU v1 = vertices_[baseIdx + 1];
        VoxelVertexGPU v2 = vertices_[baseIdx + 2];
        VoxelVertexGPU v3 = vertices_[baseIdx + 3];
        
        // Remove the 4 vertices
        vertices_.resize(baseIdx);
        
        // Add 6 vertices (2 triangles)
        if (quad.ao.shouldFlip()) {
            // Alternate triangulation: 0,1,3 and 1,2,3
            vertices_.push_back(v0);
            vertices_.push_back(v1);
            vertices_.push_back(v3);
            vertices_.push_back(v1);
            vertices_.push_back(v2);
            vertices_.push_back(v3);
        } else {
            // Standard triangulation: 0,1,2 and 0,2,3
            vertices_.push_back(v0);
            vertices_.push_back(v1);
            vertices_.push_back(v2);
            vertices_.push_back(v0);
            vertices_.push_back(v2);
            vertices_.push_back(v3);
        }
    }
}

void VertexEncoder::encodeQuad(const MergedQuad& quad, const ChunkCoord& chunkCoord) {
    uint8_t face = quad.face;
    
    // Get base vertex positions
    uint8_t vx[4], vy[4], vz[4];
    getQuadVertices(quad, vx, vy, vz);
    
    // Get texture and color
    uint8_t tex1 = textureMapper_(quad.block, face);
    uint8_t tex2 = 0;  // Secondary texture (unused for now)
    uint8_t color = colorMapper_(quad.block);
    
    // Encode 4 vertices
    for (int i = 0; i < 4; ++i) {
        VoxelVertexGPU v;
        v.attrVertex = VoxelVertexGPU::encodeVertex(
            vx[i], vy[i], vz[i],
            quad.ao.ao[i] * 85  // Scale 0-3 to 0-255 (0, 85, 170, 255)
        );
        v.attrFace = VoxelVertexGPU::encodeFace(
            tex1, tex2, color, face
        );
        vertices_.push_back(v);
    }
}

void VertexEncoder::getQuadVertices(const MergedQuad& quad,
                                    uint8_t outX[4],
                                    uint8_t outY[4],
                                    uint8_t outZ[4]) const {
    uint8_t face = quad.face;
    const uint8_t (*verts)[3] = FACE_VERTS[face];
    
    // Base position
    uint8_t bx = quad.x;
    uint8_t by = quad.y;
    uint8_t bz = quad.z;
    
    // Width and height
    uint8_t w = quad.width;
    uint8_t h = quad.height;
    
    // UV axes for this face
    int uAxis = FACE_UV_AXES[face][0];
    int vAxis = FACE_UV_AXES[face][1];
    
    for (int i = 0; i < 4; ++i) {
        // Start from base vertex offset
        int x = bx + verts[i][0];
        int y = by + verts[i][1];
        int z = bz + verts[i][2];
        
        // Scale by width/height in UV space
        // Vertices 1,2 have U=1 (should scale by width)
        // Vertices 2,3 have V=1 (should scale by height)
        int uScale = (i == 1 || i == 2) ? (w - 1) : 0;
        int vScale = (i == 2 || i == 3) ? (h - 1) : 0;
        
        // Apply scaling in the appropriate axis
        switch (uAxis) {
            case 0: x += uScale; break;
            case 1: y += uScale; break;
            case 2: z += uScale; break;
        }
        switch (vAxis) {
            case 0: x += vScale; break;
            case 1: y += vScale; break;
            case 2: z += vScale; break;
        }
        
        outX[i] = static_cast<uint8_t>(x);
        outY[i] = static_cast<uint8_t>(y);
        outZ[i] = static_cast<uint8_t>(z);
    }
}

// ============================================================================
// Index Buffer Generation
// ============================================================================

std::vector<uint32_t> generateQuadIndices(size_t quadCount, const bool* flipFlags) {
    std::vector<uint32_t> indices;
    indices.reserve(quadCount * 6);
    
    for (size_t i = 0; i < quadCount; ++i) {
        uint32_t base = static_cast<uint32_t>(i * 4);
        
        bool flip = flipFlags ? flipFlags[i] : false;
        
        if (flip) {
            // Alternate triangulation: 0,1,3 and 1,2,3
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 3);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        } else {
            // Standard triangulation: 0,1,2 and 0,2,3
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
    }
    
    return indices;
}

std::vector<uint32_t> generateQuadIndices(const std::vector<QuadAO>& aoValues) {
    std::vector<uint32_t> indices;
    indices.reserve(aoValues.size() * 6);
    
    for (size_t i = 0; i < aoValues.size(); ++i) {
        uint32_t base = static_cast<uint32_t>(i * 4);
        
        if (aoValues[i].shouldFlip()) {
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 3);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        } else {
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
    }
    
    return indices;
}

} // namespace voxel
} // namespace jupiter



