/**
 * @file ao_calculator.cpp
 * @brief Implementation of vertex ambient occlusion calculation
 */

#include <voxel/ao_calculator.h>

namespace jupiter {
namespace voxel {

// ============================================================================
// Vertex Offsets for AO Sampling
// ============================================================================

/**
 * AO sampling: for each face direction and vertex, we need:
 * - 2 side samples (edges)
 * - 1 corner sample
 *
 * Vertex order in face-local UV:
 *   3---2
 *   |   |
 *   0---1
 *
 * Each array is [face][vertex][sample][xyz]
 * Sample 0,1 = sides, sample 2 = corner
 */

// Offsets are in world space relative to the block center,
// moved to the face, then to each vertex
static const int AO_OFFSETS[6][4][3][3] = {
    // +X face (looking at block from +X direction)
    {
        {{1, -1, 0}, {1, 0, -1}, {1, -1, -1}},  // vertex 0
        {{1, -1, 0}, {1, 0,  1}, {1, -1,  1}},  // vertex 1
        {{1,  1, 0}, {1, 0,  1}, {1,  1,  1}},  // vertex 2
        {{1,  1, 0}, {1, 0, -1}, {1,  1, -1}},  // vertex 3
    },
    // -X face
    {
        {{-1, -1, 0}, {-1, 0,  1}, {-1, -1,  1}},
        {{-1, -1, 0}, {-1, 0, -1}, {-1, -1, -1}},
        {{-1,  1, 0}, {-1, 0, -1}, {-1,  1, -1}},
        {{-1,  1, 0}, {-1, 0,  1}, {-1,  1,  1}},
    },
    // +Y face (top)
    {
        {{0, 1, -1}, {-1, 1, 0}, {-1, 1, -1}},
        {{ 1, 1, 0}, {0, 1, -1}, { 1, 1, -1}},
        {{ 1, 1, 0}, {0, 1,  1}, { 1, 1,  1}},
        {{0, 1,  1}, {-1, 1, 0}, {-1, 1,  1}},
    },
    // -Y face (bottom)
    {
        {{0, -1,  1}, {-1, -1, 0}, {-1, -1,  1}},
        {{ 1, -1, 0}, {0, -1,  1}, { 1, -1,  1}},
        {{ 1, -1, 0}, {0, -1, -1}, { 1, -1, -1}},
        {{0, -1, -1}, {-1, -1, 0}, {-1, -1, -1}},
    },
    // +Z face
    {
        {{-1, 0, 1}, {0, -1, 1}, {-1, -1, 1}},
        {{ 1, 0, 1}, {0, -1, 1}, { 1, -1, 1}},
        {{ 1, 0, 1}, {0,  1, 1}, { 1,  1, 1}},
        {{-1, 0, 1}, {0,  1, 1}, {-1,  1, 1}},
    },
    // -Z face
    {
        {{ 1, 0, -1}, {0, -1, -1}, { 1, -1, -1}},
        {{-1, 0, -1}, {0, -1, -1}, {-1, -1, -1}},
        {{-1, 0, -1}, {0,  1, -1}, {-1,  1, -1}},
        {{ 1, 0, -1}, {0,  1, -1}, { 1,  1, -1}},
    },
};

// ============================================================================
// AOCalculator Implementation
// ============================================================================

void AOCalculator::process(const ChunkColumns& chunk,
                           const ChunkColumns* neighbors[6],
                           const std::vector<VisibleFace>& faces) {
    quadAO_.clear();
    quadAO_.reserve(faces.size());
    
    for (const auto& face : faces) {
        QuadAO ao;
        FaceDirection dir = static_cast<FaceDirection>(face.face);
        
        for (int v = 0; v < 4; ++v) {
            ao.ao[v] = calculateVertexAO(chunk, neighbors,
                                        face.x, face.y, face.z,
                                        dir, v);
        }
        
        quadAO_.push_back(ao);
    }
}

void AOCalculator::processFlat(const ChunkVoxelData& chunk,
                               const ChunkVoxelData* neighbors[6],
                               const std::vector<VisibleFace>& faces) {
    quadAO_.clear();
    quadAO_.reserve(faces.size());
    
    for (const auto& face : faces) {
        QuadAO ao;
        FaceDirection dir = static_cast<FaceDirection>(face.face);
        
        for (int v = 0; v < 4; ++v) {
            ao.ao[v] = calculateVertexAOFlat(chunk, neighbors,
                                            face.x, face.y, face.z,
                                            dir, v);
        }
        
        quadAO_.push_back(ao);
    }
}

uint8_t AOCalculator::calculateVertexAO(const ChunkColumns& chunk,
                                        const ChunkColumns* neighbors[6],
                                        int x, int y, int z,
                                        FaceDirection face,
                                        int vertex) const {
    const int (*offsets)[3] = AO_OFFSETS[face][vertex];
    
    bool side1 = isOccluding(chunk, neighbors,
                            x + offsets[0][0],
                            y + offsets[0][1],
                            z + offsets[0][2]);
    
    bool side2 = isOccluding(chunk, neighbors,
                            x + offsets[1][0],
                            y + offsets[1][1],
                            z + offsets[1][2]);
    
    bool corner = isOccluding(chunk, neighbors,
                             x + offsets[2][0],
                             y + offsets[2][1],
                             z + offsets[2][2]);
    
    // Minecraft-style AO formula
    if (side1 && side2) {
        return 0;  // Both sides occluded = maximum shadow
    }
    
    return static_cast<uint8_t>(3 - (side1 + side2 + corner));
}

uint8_t AOCalculator::calculateVertexAOFlat(const ChunkVoxelData& chunk,
                                            const ChunkVoxelData* neighbors[6],
                                            int x, int y, int z,
                                            FaceDirection face,
                                            int vertex) const {
    const int (*offsets)[3] = AO_OFFSETS[face][vertex];
    
    bool side1 = isOccludingFlat(chunk, neighbors,
                                x + offsets[0][0],
                                y + offsets[0][1],
                                z + offsets[0][2]);
    
    bool side2 = isOccludingFlat(chunk, neighbors,
                                x + offsets[1][0],
                                y + offsets[1][1],
                                z + offsets[1][2]);
    
    bool corner = isOccludingFlat(chunk, neighbors,
                                 x + offsets[2][0],
                                 y + offsets[2][1],
                                 z + offsets[2][2]);
    
    if (side1 && side2) {
        return 0;
    }
    
    return static_cast<uint8_t>(3 - (side1 + side2 + corner));
}

bool AOCalculator::isOccluding(const ChunkColumns& chunk,
                               const ChunkColumns* neighbors[6],
                               int x, int y, int z) const {
    BlockType block = getBlockAt(chunk, neighbors, x, y, z);
    return block != BLOCK_AIR;
}

bool AOCalculator::isOccludingFlat(const ChunkVoxelData& chunk,
                                   const ChunkVoxelData* neighbors[6],
                                   int x, int y, int z) const {
    BlockType block = getBlockAtFlat(chunk, neighbors, x, y, z);
    return block != BLOCK_AIR;
}

BlockType AOCalculator::getBlockAt(const ChunkColumns& chunk,
                                   const ChunkColumns* neighbors[6],
                                   int x, int y, int z) const {
    // Y bounds check
    if (y < 0 || y >= CHUNK_HEIGHT) {
        return BLOCK_AIR;
    }
    
    // Determine which chunk and local coords
    int chunkX = 0, chunkZ = 0;
    int localX = x, localZ = z;
    
    if (x < 0) {
        chunkX = -1;
        localX = x + CHUNK_SIZE;
    } else if (x >= CHUNK_SIZE) {
        chunkX = 1;
        localX = x - CHUNK_SIZE;
    }
    
    if (z < 0) {
        chunkZ = -1;
        localZ = z + CHUNK_SIZE;
    } else if (z >= CHUNK_SIZE) {
        chunkZ = 1;
        localZ = z - CHUNK_SIZE;
    }
    
    // Determine neighbor index
    if (chunkX == 0 && chunkZ == 0) {
        return chunk.at(localX, localZ).getBlock(y);
    }
    
    // Get appropriate neighbor
    const ChunkColumns* neighbor = nullptr;
    if (chunkX == 1 && chunkZ == 0) {
        neighbor = neighbors[FACE_POS_X];
    } else if (chunkX == -1 && chunkZ == 0) {
        neighbor = neighbors[FACE_NEG_X];
    } else if (chunkX == 0 && chunkZ == 1) {
        neighbor = neighbors[FACE_POS_Z];
    } else if (chunkX == 0 && chunkZ == -1) {
        neighbor = neighbors[FACE_NEG_Z];
    }
    // Corner cases - we'd need diagonal neighbors which we don't have
    // Just return air for simplicity (minor visual artifact at corners)
    
    if (!neighbor) {
        return BLOCK_AIR;
    }
    
    return neighbor->at(localX, localZ).getBlock(y);
}

BlockType AOCalculator::getBlockAtFlat(const ChunkVoxelData& chunk,
                                       const ChunkVoxelData* neighbors[6],
                                       int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) {
        return BLOCK_AIR;
    }
    
    int chunkX = 0, chunkZ = 0;
    int localX = x, localZ = z;
    
    if (x < 0) {
        chunkX = -1;
        localX = x + CHUNK_SIZE;
    } else if (x >= CHUNK_SIZE) {
        chunkX = 1;
        localX = x - CHUNK_SIZE;
    }
    
    if (z < 0) {
        chunkZ = -1;
        localZ = z + CHUNK_SIZE;
    } else if (z >= CHUNK_SIZE) {
        chunkZ = 1;
        localZ = z - CHUNK_SIZE;
    }
    
    if (chunkX == 0 && chunkZ == 0) {
        return chunk.getBlock(localX, y, localZ);
    }
    
    const ChunkVoxelData* neighbor = nullptr;
    if (chunkX == 1 && chunkZ == 0) {
        neighbor = neighbors[FACE_POS_X];
    } else if (chunkX == -1 && chunkZ == 0) {
        neighbor = neighbors[FACE_NEG_X];
    } else if (chunkX == 0 && chunkZ == 1) {
        neighbor = neighbors[FACE_POS_Z];
    } else if (chunkX == 0 && chunkZ == -1) {
        neighbor = neighbors[FACE_NEG_Z];
    }
    
    if (!neighbor) {
        return BLOCK_AIR;
    }
    
    return neighbor->getBlock(localX, y, localZ);
}

} // namespace voxel
} // namespace jupiter



