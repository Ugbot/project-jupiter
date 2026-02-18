/**
 * @file face_culler.cpp
 * @brief Implementation of face culling for voxel meshing
 */

#include <voxel/face_culler.h>
#include <cstring>

namespace jupiter {
namespace voxel {

void FaceCuller::process(const ChunkColumns& chunk,
                         const ChunkColumns* neighbors[6]) {
    visibleFaces_.clear();
    
    // Pre-size for typical case (rough estimate)
    visibleFaces_.reserve(CHUNK_SIZE * CHUNK_SIZE * 16);
    
    // Iterate all columns
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            const VoxelColumn& col = chunk.at(x, z);
            
            // Skip empty columns
            if (col.isEmpty()) continue;
            
            // Only check Y range with solid blocks
            int minY = col.minSolidY;
            int maxY = col.maxSolidY;
            
            for (int y = minY; y <= maxY; ++y) {
                BlockType block = col.getBlock(y);
                
                // Skip transparent blocks
                if (isTransparent(block)) continue;
                
                // Check each face direction
                for (int face = 0; face < FACE_COUNT; ++face) {
                    if (isNeighborTransparent(chunk, neighbors, x, y, z,
                                             static_cast<FaceDirection>(face))) {
                        VisibleFace vf;
                        vf.x = static_cast<uint8_t>(x);
                        vf.y = static_cast<uint8_t>(y);
                        vf.z = static_cast<uint8_t>(z);
                        vf.face = static_cast<uint8_t>(face);
                        vf.block = block;
                        visibleFaces_.push_back(vf);
                    }
                }
            }
        }
    }
}

void FaceCuller::processFlat(const ChunkVoxelData& chunk,
                             const ChunkVoxelData* neighbors[6]) {
    visibleFaces_.clear();
    visibleFaces_.reserve(CHUNK_SIZE * CHUNK_SIZE * 16);
    
    // Iterate all voxels
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                BlockType block = chunk.getBlock(x, y, z);
                
                // Skip transparent blocks
                if (isTransparent(block)) continue;
                
                // Check each face direction
                for (int face = 0; face < FACE_COUNT; ++face) {
                    if (isNeighborTransparentFlat(chunk, neighbors, x, y, z,
                                                  static_cast<FaceDirection>(face))) {
                        VisibleFace vf;
                        vf.x = static_cast<uint8_t>(x);
                        vf.y = static_cast<uint8_t>(y);
                        vf.z = static_cast<uint8_t>(z);
                        vf.face = static_cast<uint8_t>(face);
                        vf.block = block;
                        visibleFaces_.push_back(vf);
                    }
                }
            }
        }
    }
}

bool FaceCuller::isNeighborTransparent(const ChunkColumns& chunk,
                                       const ChunkColumns* neighbors[6],
                                       int x, int y, int z,
                                       FaceDirection face) const {
    int nx = x + FACE_NORMALS[face][0];
    int ny = y + FACE_NORMALS[face][1];
    int nz = z + FACE_NORMALS[face][2];
    
    // Check Y bounds (no neighbor chunks in Y for tall chunks)
    if (ny < 0 || ny >= CHUNK_HEIGHT) {
        return true;  // Exposed at world bounds
    }
    
    // Check if neighbor is in adjacent chunk
    if (nx < 0) {
        // -X neighbor chunk
        if (!neighbors[FACE_NEG_X]) return true;
        return isTransparent(neighbors[FACE_NEG_X]->at(CHUNK_SIZE - 1, z).getBlock(ny));
    }
    if (nx >= CHUNK_SIZE) {
        // +X neighbor chunk
        if (!neighbors[FACE_POS_X]) return true;
        return isTransparent(neighbors[FACE_POS_X]->at(0, z).getBlock(ny));
    }
    if (nz < 0) {
        // -Z neighbor chunk
        if (!neighbors[FACE_NEG_Z]) return true;
        return isTransparent(neighbors[FACE_NEG_Z]->at(x, CHUNK_SIZE - 1).getBlock(ny));
    }
    if (nz >= CHUNK_SIZE) {
        // +Z neighbor chunk
        if (!neighbors[FACE_POS_Z]) return true;
        return isTransparent(neighbors[FACE_POS_Z]->at(x, 0).getBlock(ny));
    }
    
    // Neighbor is within this chunk
    return isTransparent(chunk.at(nx, nz).getBlock(ny));
}

bool FaceCuller::isNeighborTransparentFlat(const ChunkVoxelData& chunk,
                                           const ChunkVoxelData* neighbors[6],
                                           int x, int y, int z,
                                           FaceDirection face) const {
    int nx = x + FACE_NORMALS[face][0];
    int ny = y + FACE_NORMALS[face][1];
    int nz = z + FACE_NORMALS[face][2];
    
    // Check Y bounds
    if (ny < 0 || ny >= CHUNK_HEIGHT) {
        return true;
    }
    
    // Check if neighbor is in adjacent chunk
    if (nx < 0) {
        if (!neighbors[FACE_NEG_X]) return true;
        return isTransparent(neighbors[FACE_NEG_X]->getBlock(CHUNK_SIZE - 1, ny, z));
    }
    if (nx >= CHUNK_SIZE) {
        if (!neighbors[FACE_POS_X]) return true;
        return isTransparent(neighbors[FACE_POS_X]->getBlock(0, ny, z));
    }
    if (nz < 0) {
        if (!neighbors[FACE_NEG_Z]) return true;
        return isTransparent(neighbors[FACE_NEG_Z]->getBlock(x, ny, CHUNK_SIZE - 1));
    }
    if (nz >= CHUNK_SIZE) {
        if (!neighbors[FACE_POS_Z]) return true;
        return isTransparent(neighbors[FACE_POS_Z]->getBlock(x, ny, 0));
    }
    
    // Neighbor is within this chunk
    return isTransparent(chunk.getBlock(nx, ny, nz));
}

} // namespace voxel
} // namespace jupiter



