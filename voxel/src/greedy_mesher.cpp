/**
 * @file greedy_mesher.cpp
 * @brief Implementation of greedy meshing algorithm
 */

#include <voxel/greedy_mesher.h>
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace voxel {

// ============================================================================
// Face UV Axis Mapping
// ============================================================================

/**
 * For each face direction, define which world axes map to face-local U and V.
 * [face][0] = U axis (0=X, 1=Y, 2=Z)
 * [face][1] = V axis
 * [face][2] = depth axis (normal direction)
 */
static constexpr int FACE_AXES[6][3] = {
    {2, 1, 0},  // +X: U=Z, V=Y, depth=X
    {2, 1, 0},  // -X: U=Z, V=Y, depth=X
    {0, 2, 1},  // +Y: U=X, V=Z, depth=Y
    {0, 2, 1},  // -Y: U=X, V=Z, depth=Y
    {0, 1, 2},  // +Z: U=X, V=Y, depth=Z
    {0, 1, 2},  // -Z: U=X, V=Y, depth=Z
};

/**
 * Size in each axis for the slice
 */
static constexpr int SLICE_SIZE[3] = {CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE};

// Helper to get position component by axis
inline int getAxis(const VisibleFace& f, int axis) {
    switch (axis) {
        case 0: return f.x;
        case 1: return f.y;
        case 2: return f.z;
        default: return 0;
    }
}

// ============================================================================
// GreedyMesher Implementation
// ============================================================================

void GreedyMesher::process(const std::vector<VisibleFace>& faces,
                           const std::vector<QuadAO>& ao) {
    quads_.clear();
    
    if (faces.empty()) return;
    
    if (!greedyEnabled_) {
        // No merging - each face becomes a 1x1 quad
        quads_.reserve(faces.size());
        
        for (size_t i = 0; i < faces.size(); ++i) {
            const auto& face = faces[i];
            
            MergedQuad quad;
            quad.x = face.x;
            quad.y = face.y;
            quad.z = face.z;
            quad.face = face.face;
            quad.width = 1;
            quad.height = 1;
            quad.block = face.block;
            quad.ao = (i < ao.size()) ? ao[i] : QuadAO{{3, 3, 3, 3}};
            
            quads_.push_back(quad);
        }
        return;
    }
    
    // Group faces by direction and depth
    for (int faceDir = 0; faceDir < 6; ++faceDir) {
        int depthAxis = FACE_AXES[faceDir][2];
        int depthSize = SLICE_SIZE[depthAxis];
        
        // Process each depth slice
        for (int depth = 0; depth < depthSize; ++depth) {
            // Collect faces in this slice
            std::vector<size_t> sliceFaces;
            
            for (size_t i = 0; i < faces.size(); ++i) {
                if (faces[i].face == faceDir &&
                    getAxis(faces[i], depthAxis) == depth) {
                    sliceFaces.push_back(i);
                }
            }
            
            if (sliceFaces.empty()) continue;
            
            // Greedy merge this slice
            greedyMergeSlice(faces, ao, sliceFaces, static_cast<FaceDirection>(faceDir), depth);
        }
    }
}

void GreedyMesher::processNoAO(const std::vector<VisibleFace>& faces) {
    // Create dummy AO with full brightness
    std::vector<QuadAO> ao(faces.size());
    for (auto& a : ao) {
        a.ao[0] = a.ao[1] = a.ao[2] = a.ao[3] = 3;
    }
    
    process(faces, ao);
}

void GreedyMesher::greedyMergeSlice(const std::vector<VisibleFace>& allFaces,
                                    const std::vector<QuadAO>& ao,
                                    const std::vector<size_t>& faceIndices,
                                    FaceDirection faceDir,
                                    int depth) {
    int uAxis = FACE_AXES[faceDir][0];
    int vAxis = FACE_AXES[faceDir][1];
    int uSize = SLICE_SIZE[uAxis];
    int vSize = SLICE_SIZE[vAxis];
    
    // Create 2D mask: -1 = empty, >= 0 = index into faceIndices
    mask_.assign(uSize * vSize, -1);
    aoMask_.assign(uSize * vSize, 0);
    
    // Fill mask
    for (size_t fi = 0; fi < faceIndices.size(); ++fi) {
        size_t idx = faceIndices[fi];
        const auto& face = allFaces[idx];
        
        int u = getAxis(face, uAxis);
        int v = getAxis(face, vAxis);
        
        mask_[u + v * uSize] = static_cast<int32_t>(idx);
        
        if (idx < ao.size()) {
            aoMask_[u + v * uSize] = ao[idx].pack();
        } else {
            aoMask_[u + v * uSize] = 0xFF;  // Full brightness
        }
    }
    
    // Greedy merge
    for (int v = 0; v < vSize; ++v) {
        for (int u = 0; u < uSize; ) {
            int32_t faceIdx = mask_[u + v * uSize];
            
            if (faceIdx < 0) {
                ++u;
                continue;
            }
            
            const auto& face = allFaces[faceIdx];
            uint8_t aoVal = aoMask_[u + v * uSize];
            
            // Find width (extend in U direction)
            int width = 1;
            while (u + width < uSize) {
                int32_t nextIdx = mask_[u + width + v * uSize];
                if (nextIdx < 0) break;
                
                const auto& nextFace = allFaces[nextIdx];
                
                // Must match block type
                if (nextFace.block != face.block) break;
                
                // Optionally check AO match
                if (requireMatchingAO_) {
                    if (aoMask_[u + width + v * uSize] != aoVal) break;
                }
                
                ++width;
            }
            
            // Find height (extend in V direction)
            int height = 1;
            bool heightDone = false;
            
            while (v + height < vSize && !heightDone) {
                for (int du = 0; du < width; ++du) {
                    int32_t checkIdx = mask_[u + du + (v + height) * uSize];
                    
                    if (checkIdx < 0) {
                        heightDone = true;
                        break;
                    }
                    
                    const auto& checkFace = allFaces[checkIdx];
                    
                    if (checkFace.block != face.block) {
                        heightDone = true;
                        break;
                    }
                    
                    if (requireMatchingAO_) {
                        if (aoMask_[u + du + (v + height) * uSize] != aoVal) {
                            heightDone = true;
                            break;
                        }
                    }
                }
                
                if (!heightDone) {
                    ++height;
                }
            }
            
            // Create merged quad
            MergedQuad quad;
            quad.x = face.x;
            quad.y = face.y;
            quad.z = face.z;
            quad.face = static_cast<uint8_t>(faceDir);
            quad.width = static_cast<uint8_t>(width);
            quad.height = static_cast<uint8_t>(height);
            quad.block = face.block;
            
            // Use the AO from the first face
            if (faceIdx < static_cast<int32_t>(ao.size())) {
                quad.ao = ao[faceIdx];
            } else {
                quad.ao = QuadAO{{3, 3, 3, 3}};
            }
            
            quads_.push_back(quad);
            
            // Clear used cells in mask
            for (int dv = 0; dv < height; ++dv) {
                for (int du = 0; du < width; ++du) {
                    mask_[u + du + (v + dv) * uSize] = -1;
                }
            }
            
            u += width;
        }
    }
}

} // namespace voxel
} // namespace jupiter



