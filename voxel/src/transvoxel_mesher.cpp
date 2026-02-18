/**
 * @file transvoxel_mesher.cpp
 * @brief Implementation of Transvoxel transition cell mesher
 */

#include <voxel/transvoxel_mesher.h>
#include <voxel/transvoxel_tables.h>
#include <cmath>

namespace jupiter {
namespace voxel {

// ============================================================================
// Transition Cell Edge Endpoints
// ============================================================================

// 10 edges in a transition cell, each connects two of the 13 samples
// Samples 0-8: high-res face (9 samples: 4 corners + 4 edge midpoints + center)
// Samples 9-12: low-res corners (from neighbor chunk)
static const int transitionEdges[10][2] = {
    {0, 1}, {1, 2},           // Bottom edge segments
    {3, 4}, {4, 5},           // Middle edge segments
    {6, 7}, {7, 8},           // Top edge segments
    {0, 3}, {3, 6},           // Left edge segments
    {2, 5}, {5, 8},           // Right edge segments
};

// ============================================================================
// TransvoxelMesher Implementation
// ============================================================================

void TransvoxelMesher::processTransition(const ChunkColumns& highResChunk,
                                          const ChunkColumns& lowResChunk,
                                          FaceDirection boundaryFace,
                                          const ChunkCoord& highResCoord,
                                          SmoothMeshBuffer& output) {
    resetStats();
    
    // World offset
    const float worldX = static_cast<float>(highResCoord.x * CHUNK_SIZE);
    const float worldY = static_cast<float>(highResCoord.y * CHUNK_SIZE);
    const float worldZ = static_cast<float>(highResCoord.z * CHUNK_SIZE);
    
    // Transition cells are on chunk faces
    // We iterate in 2D on the face, with depth perpendicular
    
    // Determine iteration axes based on face
    int uAxis = 0, vAxis = 1;  // Which axes to iterate
    int faceOffset = 0;        // Where the face is
    
    switch (boundaryFace) {
        case FACE_POS_X:
            uAxis = 2; vAxis = 1;
            faceOffset = CHUNK_SIZE - 1;
            break;
        case FACE_NEG_X:
            uAxis = 2; vAxis = 1;
            faceOffset = 0;
            break;
        case FACE_POS_Y:
            uAxis = 0; vAxis = 2;
            faceOffset = CHUNK_HEIGHT - 1;
            break;
        case FACE_NEG_Y:
            uAxis = 0; vAxis = 2;
            faceOffset = 0;
            break;
        case FACE_POS_Z:
            uAxis = 0; vAxis = 1;
            faceOffset = CHUNK_SIZE - 1;
            break;
        case FACE_NEG_Z:
            uAxis = 0; vAxis = 1;
            faceOffset = 0;
            break;
        default:
            break;
    }
    
    // Transition cells are 2x2 in high-res, 1x1 in low-res
    // So we process every 2 voxels
    // Note: We iterate to maxV/maxU (not maxV-2) to ensure boundary cells are processed
    int maxU = (uAxis == 1) ? CHUNK_HEIGHT : CHUNK_SIZE;
    int maxV = (vAxis == 1) ? CHUNK_HEIGHT : CHUNK_SIZE;
    
    // Process transition cells along the boundary face
    // Each cell covers 2x2 high-res samples, so we iterate by 2
    for (int v = 0; v < maxV; v += 2) {
        for (int u = 0; u < maxU; u += 2) {
            // Skip if this cell would extend beyond bounds
            if (u + 2 > maxU || v + 2 > maxV) continue;
            // Sample 9 high-res points (3x3 grid)
            float densities[13];
            BlockType materials[9];
            
            for (int sv = 0; sv < 3; ++sv) {
                for (int su = 0; su < 3; ++su) {
                    int idx = sv * 3 + su;
                    
                    // Convert UV to XYZ
                    int pos[3] = {0, 0, 0};
                    pos[uAxis] = u + su;
                    pos[vAxis] = v + sv;
                    
                    // Set the face coordinate
                    if (boundaryFace == FACE_POS_X ||
                        boundaryFace == FACE_NEG_X) {
                        pos[0] = faceOffset;
                    } else if (boundaryFace == FACE_POS_Y ||
                               boundaryFace == FACE_NEG_Y) {
                        pos[1] = faceOffset;
                    } else {
                        pos[2] = faceOffset;
                    }
                    
                    // Clamp to valid range
                    pos[0] = std::max(0, std::min(CHUNK_SIZE - 1, pos[0]));
                    pos[1] = std::max(0, std::min(CHUNK_HEIGHT - 1, pos[1]));
                    pos[2] = std::max(0, std::min(CHUNK_SIZE - 1, pos[2]));
                    
                    densities[idx] = highResChunk.getDensity(pos[0], pos[1], pos[2]);
                    materials[idx] = highResChunk.at(pos[0], pos[2]).getBlock(pos[1]);
                }
            }
            
            // Sample 4 low-res corners from neighbor
            // These correspond to the 4 corners of the 2x2 high-res cell
            // mapped to the low-res chunk
            for (int sv = 0; sv < 2; ++sv) {
                for (int su = 0; su < 2; ++su) {
                    int idx = 9 + sv * 2 + su;
                    
                    // Low-res position (half resolution)
                    int lowU = (u / 2) + su;
                    int lowV = (v / 2) + sv;
                    
                    // Convert to XYZ in neighbor chunk
                    int pos[3] = {0, 0, 0};
                    pos[uAxis] = lowU;
                    pos[vAxis] = lowV;
                    
                    // The neighbor is on the opposite side
                    switch (boundaryFace) {
                        case FACE_POS_X:
                            pos[0] = 0;  // Start of neighbor
                            break;
                        case FACE_NEG_X:
                            pos[0] = CHUNK_SIZE - 1;
                            break;
                        case FACE_POS_Y:
                            pos[1] = 0;
                            break;
                        case FACE_NEG_Y:
                            pos[1] = CHUNK_HEIGHT - 1;
                            break;
                        case FACE_POS_Z:
                            pos[2] = 0;
                            break;
                        case FACE_NEG_Z:
                            pos[2] = CHUNK_SIZE - 1;
                            break;
                        default:
                            break;
                    }
                    
                    // Clamp
                    pos[0] = std::max(0, std::min(CHUNK_SIZE - 1, pos[0]));
                    pos[1] = std::max(0, std::min(CHUNK_HEIGHT - 1, pos[1]));
                    pos[2] = std::max(0, std::min(CHUNK_SIZE - 1, pos[2]));
                    
                    densities[idx] = lowResChunk.getDensity(pos[0], pos[1], pos[2]);
                }
            }
            
            // Compute cell origin in LOCAL chunk space (not world space)
            // The shader will add chunk offset to convert to world space
            glm::vec3 cellOrigin(0.0f);
            cellOrigin[uAxis] = static_cast<float>(u);
            cellOrigin[vAxis] = static_cast<float>(v);
            
            // Set the face coordinate based on boundary face
            switch (boundaryFace) {
                case FACE_POS_X: cellOrigin.x = static_cast<float>(faceOffset); break;
                case FACE_NEG_X: cellOrigin.x = 0.0f; break;
                case FACE_POS_Y: cellOrigin.y = static_cast<float>(faceOffset); break;
                case FACE_NEG_Y: cellOrigin.y = 0.0f; break;
                case FACE_POS_Z: cellOrigin.z = static_cast<float>(faceOffset); break;
                case FACE_NEG_Z: cellOrigin.z = 0.0f; break;
                default: break;
            }
            
            // Process this transition cell
            processTransitionCell(densities, materials, cellOrigin, boundaryFace, output);
        }
    }
    
    output.hasTransitions = true;
}

void TransvoxelMesher::processTransitionCell(const float densities[13],
                                              const BlockType materials[9],
                                              const glm::vec3& cellOrigin,
                                              FaceDirection faceDir,
                                              SmoothMeshBuffer& output) {
    // Compute case index from the 9 high-res samples
    uint16_t caseIndex = computeTransitionCaseIndex(densities);
    
    // Skip empty/full cells
    if (caseIndex == 0 || caseIndex == 511) {
        return;
    }
    
    cellsProcessed_++;
    
    // Get cell class
    uint8_t cellClass = transvoxel::transitionCellClass[caseIndex];
    uint8_t classIndex = transvoxel::getTransitionClassIndex(cellClass);
    bool invert = transvoxel::isTransitionInverted(cellClass);
    
    // Get cell data
    const transvoxel::TransitionCellData& cellData = transvoxel::transitionCellData[classIndex];
    int numTriangles = cellData.getTriangleCount();
    
    if (numTriangles == 0) return;
    
    // Get sample positions
    glm::vec3 positions[13];
    getTransitionPositions(cellOrigin, faceDir, 2.0f, positions);
    
        // Determine dominant material
        BlockType material = materials[4];  // Center sample as default
        for (int i = 0; i < 9; ++i) {
            if (densities[i] < isoLevel_ && materials[i] != BLOCK_AIR) {
                material = materials[i];
                break;
            }
        }
        
        // Compute approximate gradients from density differences
        // This gives smooth normals without needing full chunk access
        glm::vec3 gradients[13];
        
        // For high-res samples (0-8), compute gradients from neighbors in the 3x3 grid
        // Grid layout: 0 1 2
        //              3 4 5
        //              6 7 8
        for (int i = 0; i < 9; ++i) {
            int row = i / 3;
            int col = i % 3;
            
            // Sample neighbors (with bounds checking)
            float nx = (col > 0) ? densities[i - 1] : densities[i];
            float px = (col < 2) ? densities[i + 1] : densities[i];
            float ny = (row > 0) ? densities[i - 3] : densities[i];
            float py = (row < 2) ? densities[i + 3] : densities[i];
            
            // Compute gradient (points from inside to outside)
            glm::vec3 grad(nx - px, ny - py, 0.0f);
            float len = glm::length(grad);
            if (len < 0.0001f) {
                grad = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                grad = grad / len;
            }
            gradients[i] = grad;
        }
        
        // For low-res samples (9-12), use average of nearby high-res samples
        // 9 = corner (0), 10 = corner (2), 11 = corner (6), 12 = corner (8)
        gradients[9] = gradients[0];   // Map to sample 0
        gradients[10] = gradients[2];  // Map to sample 2
        gradients[11] = gradients[6];  // Map to sample 6
        gradients[12] = gradients[8];  // Map to sample 8
        
        // Interpolate vertices along edges where surface crosses
        // The vertex indices in cellData reference the 13 sample positions
        // We need to interpolate along edges when the surface crosses them
        
        // Helper to interpolate along an edge
        auto interpolateEdge = [this](const glm::vec3& p0, const glm::vec3& p1,
                                       float d0, float d1) -> glm::vec3 {
            float diff = d1 - d0;
            if (std::abs(diff) < 0.0001f) {
                return (p0 + p1) * 0.5f;
            }
            float t = (isoLevel_ - d0) / diff;
            t = glm::clamp(t, 0.0f, 1.0f);
            return p0 + t * (p1 - p0);
        };
        
        // Helper to interpolate normal along an edge
        auto interpolateNormal = [this](const glm::vec3& g0, const glm::vec3& g1,
                                         float d0, float d1) -> glm::vec3 {
            float diff = d1 - d0;
            if (std::abs(diff) < 0.0001f) {
                return glm::normalize(g0 + g1);
            }
            float t = (isoLevel_ - d0) / diff;
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 interpolated = g0 * (1.0f - t) + g1 * t;
            float len = glm::length(interpolated);
            if (len < 0.0001f) {
                return glm::vec3(0.0f, 1.0f, 0.0f);
            }
            return interpolated / len;
        };
    
    // Define all possible edges in transition cell
    // High-res face edges (10 edges from transitionEdgeEndpoints)
    // Plus edges connecting high-res to low-res samples
    struct Edge {
        int s0, s1;
    };
    static const Edge edges[] = {
        // High-res face edges (3x3 grid)
        {0, 1}, {1, 2},           // Bottom row
        {3, 4}, {4, 5},           // Middle row
        {6, 7}, {7, 8},           // Top row
        {0, 3}, {3, 6},           // Left column
        {2, 5}, {5, 8},           // Right column
        // High-res to low-res edges (corners map to low-res)
        {0, 9}, {2, 10}, {6, 11}, {8, 12},  // Corner to corner
        // Additional edges for smooth transitions
        {1, 9}, {1, 10},          // Bottom edge midpoints
        {7, 11}, {7, 12},         // Top edge midpoints
        {3, 9}, {3, 11},          // Left edge midpoints
        {5, 10}, {5, 12},         // Right edge midpoints
        {4, 9}, {4, 10}, {4, 11}, {4, 12},  // Center to corners
    };
    const int numEdges = sizeof(edges) / sizeof(edges[0]);
    
    // Generate triangles
    for (int t = 0; t < numTriangles; ++t) {
        SmoothVertex verts[3];
        
        for (int v = 0; v < 3; ++v) {
            int vertIdx = cellData.vertexIndex[t * 3 + v];
            int sampleIdx = vertIdx;
            if (sampleIdx >= 13) sampleIdx = sampleIdx % 13;
            
            glm::vec3 vertexPos = positions[sampleIdx];
            
            // Try to interpolate along an edge that crosses the iso-level
            for (int e = 0; e < numEdges; ++e) {
                int s0 = edges[e].s0;
                int s1 = edges[e].s1;
                
                // Check if this vertex index corresponds to an edge endpoint
                if (sampleIdx == s0 || sampleIdx == s1) {
                    // Check if edge crosses iso-level
                    bool sign0 = densities[s0] < isoLevel_;
                    bool sign1 = densities[s1] < isoLevel_;
                    
                    if (sign0 != sign1) {
                        // Edge crosses surface - interpolate
                        vertexPos = interpolateEdge(positions[s0], positions[s1],
                                                     densities[s0], densities[s1]);
                        break;
                    }
                }
            }
            
            verts[v].position = vertexPos;
            verts[v].materialId = material;
            verts[v].ao = 255;
        }
        
        // Compute smooth normals using gradients
        // Interpolate gradients along edges where vertices were interpolated
        for (int v = 0; v < 3; ++v) {
            int vertIdx = cellData.vertexIndex[t * 3 + v];
            int sampleIdx = vertIdx;
            if (sampleIdx >= 13) sampleIdx = sampleIdx % 13;
            
            glm::vec3 normal = gradients[sampleIdx];
            
            // If vertex was interpolated, interpolate the normal too
            for (int e = 0; e < numEdges; ++e) {
                int s0 = edges[e].s0;
                int s1 = edges[e].s1;
                
                if (sampleIdx == s0 || sampleIdx == s1) {
                    bool sign0 = densities[s0] < isoLevel_;
                    bool sign1 = densities[s1] < isoLevel_;
                    
                    if (sign0 != sign1) {
                        // Edge crosses surface - interpolate normal
                        float diff = densities[s1] - densities[s0];
                        if (std::abs(diff) > 0.0001f) {
                            float t = (isoLevel_ - densities[s0]) / diff;
                            t = glm::clamp(t, 0.0f, 1.0f);
                            
                            glm::vec3 g0 = gradients[s0];
                            glm::vec3 g1 = gradients[s1];
                            normal = g0 * (1.0f - t) + g1 * t;
                            float len = glm::length(normal);
                            if (len > 0.0001f) {
                                normal = normal / len;
                            }
                        }
                        break;
                    }
                }
            }
            
            verts[v].normal = normal;
        }
        
        // Invert winding if needed
        if (invert) {
            // Reverse normals and swap vertices
            for (int v = 0; v < 3; ++v) {
                verts[v].normal = -verts[v].normal;
            }
            std::swap(verts[1], verts[2]);
        }
        
        output.addTriangle(verts[0], verts[1], verts[2]);
        trianglesGenerated_++;
    }
}

uint16_t TransvoxelMesher::computeTransitionCaseIndex(const float densities[9]) const {
    uint16_t caseIndex = 0;
    
    for (int i = 0; i < 9; ++i) {
        if (densities[i] < isoLevel_) {
            caseIndex |= (1 << i);
        }
    }
    
    return caseIndex;
}

void TransvoxelMesher::getTransitionPositions(const glm::vec3& cellOrigin,
                                               FaceDirection face,
                                               float cellSize,
                                               glm::vec3 positions[13]) const {
    // The 9 high-res samples form a 3x3 grid on the face
    // The 4 low-res samples are at the corners of a 2x2 cell behind
    
    // Get face axes
    int uAxis = 0, vAxis = 1, depthAxis = 2;
    float depthDir = 1.0f;
    
    switch (face) {
        case FACE_POS_X:
            uAxis = 2; vAxis = 1; depthAxis = 0;
            depthDir = 1.0f;
            break;
        case FACE_NEG_X:
            uAxis = 2; vAxis = 1; depthAxis = 0;
            depthDir = -1.0f;
            break;
        case FACE_POS_Y:
            uAxis = 0; vAxis = 2; depthAxis = 1;
            depthDir = 1.0f;
            break;
        case FACE_NEG_Y:
            uAxis = 0; vAxis = 2; depthAxis = 1;
            depthDir = -1.0f;
            break;
        case FACE_POS_Z:
            uAxis = 0; vAxis = 1; depthAxis = 2;
            depthDir = 1.0f;
            break;
        case FACE_NEG_Z:
            uAxis = 0; vAxis = 1; depthAxis = 2;
            depthDir = -1.0f;
            break;
        default:
            break;
    }
    
    float halfCell = cellSize * 0.5f;
    
    // 9 high-res samples (3x3 grid on face)
    for (int v = 0; v < 3; ++v) {
        for (int u = 0; u < 3; ++u) {
            int idx = v * 3 + u;
            positions[idx] = cellOrigin;
            positions[idx][uAxis] += u * halfCell;
            positions[idx][vAxis] += v * halfCell;
        }
    }
    
    // 4 low-res samples (behind the face, at corners)
    float depth = cellSize * depthDir;
    for (int v = 0; v < 2; ++v) {
        for (int u = 0; u < 2; ++u) {
            int idx = 9 + v * 2 + u;
            positions[idx] = cellOrigin;
            positions[idx][uAxis] += u * cellSize;
            positions[idx][vAxis] += v * cellSize;
            positions[idx][depthAxis] += depth;
        }
    }
}

float TransvoxelMesher::sampleTransitionDensity(const ChunkColumns& chunk,
                                                 FaceDirection face,
                                                 int u, int v, int depth,
                                                 int lod) const {
    // Convert face-relative coordinates to chunk coordinates
    int x = 0, y = 0, z = 0;
    
    switch (face) {
        case FACE_POS_X:
            x = CHUNK_SIZE - 1 - depth;
            z = u; y = v;
            break;
        case FACE_NEG_X:
            x = depth;
            z = u; y = v;
            break;
        case FACE_POS_Y:
            y = CHUNK_HEIGHT - 1 - depth;
            x = u; z = v;
            break;
        case FACE_NEG_Y:
            y = depth;
            x = u; z = v;
            break;
        case FACE_POS_Z:
            z = CHUNK_SIZE - 1 - depth;
            x = u; y = v;
            break;
        case FACE_NEG_Z:
            z = depth;
            x = u; y = v;
            break;
        default:
            break;
    }
    
    // Apply LOD scale
    x = (x / lod) * lod;
    y = (y / lod) * lod;
    z = (z / lod) * lod;
    
    // Clamp
    x = std::max(0, std::min(CHUNK_SIZE - 1, x));
    y = std::max(0, std::min(CHUNK_HEIGHT - 1, y));
    z = std::max(0, std::min(CHUNK_SIZE - 1, z));
    
    return chunk.getDensity(x, y, z);
}

} // namespace voxel
} // namespace jupiter

