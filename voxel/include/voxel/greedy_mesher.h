#pragma once

/**
 * @file greedy_mesher.h
 * @brief Greedy meshing algorithm for merging adjacent coplanar faces
 *
 * Reduces polygon count by merging adjacent faces with the same
 * block type and AO values into larger quads.
 */

#include "voxel_types.h"
#include "face_culler.h"
#include "ao_calculator.h"
#include <vector>
#include <cstdint>

namespace jupiter {
namespace voxel {

// ============================================================================
// MergedQuad
// ============================================================================

/**
 * @brief A merged quad from greedy meshing
 *
 * Contains position, size, block type, and AO for GPU vertex generation.
 */
struct MergedQuad {
    // Position (corner with smallest U,V in face-local space)
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t face;       ///< Face direction (0-5)
    
    // Size in face-local UV space
    uint8_t width;      ///< Width (U direction)
    uint8_t height;     ///< Height (V direction)
    
    // Material
    BlockType block;
    
    // Padding
    uint8_t padding1;
    
    // AO values for 4 vertices
    QuadAO ao;
    
    // Padding for 16-byte alignment
    uint8_t padding2[4];
};

static_assert(sizeof(MergedQuad) == 16, "MergedQuad should be 16 bytes");

// ============================================================================
// GreedyMesher
// ============================================================================

/**
 * @brief Merges adjacent coplanar faces into larger quads
 *
 * Algorithm:
 * 1. For each face direction, create a 2D slice of the chunk
 * 2. Use greedy rectangle packing to merge adjacent same-type faces
 * 3. Only merge faces with matching block type and AO values
 *
 * This significantly reduces vertex count for large flat surfaces.
 */
class GreedyMesher {
public:
    GreedyMesher() = default;
    ~GreedyMesher() = default;
    
    // ========================================================================
    // Processing
    // ========================================================================
    
    /**
     * @brief Process visible faces and produce merged quads
     *
     * @param faces Visible faces from FaceCuller
     * @param ao AO values from AOCalculator (same order as faces)
     */
    void process(const std::vector<VisibleFace>& faces,
                 const std::vector<QuadAO>& ao);
    
    /**
     * @brief Process without AO (all vertices get full brightness)
     */
    void processNoAO(const std::vector<VisibleFace>& faces);
    
    // ========================================================================
    // Results
    // ========================================================================
    
    /**
     * @brief Get merged quads after processing
     */
    const std::vector<MergedQuad>& quads() const {
        return quads_;
    }
    
    std::vector<MergedQuad>& quads() {
        return quads_;
    }
    
    /**
     * @brief Get number of merged quads
     */
    size_t quadCount() const {
        return quads_.size();
    }
    
    /**
     * @brief Clear output
     */
    void clear() {
        quads_.clear();
    }
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Enable/disable greedy merging
     *
     * When disabled, each visible face becomes its own 1x1 quad.
     */
    void setGreedyEnabled(bool enabled) {
        greedyEnabled_ = enabled;
    }
    
    /**
     * @brief Set whether to require matching AO for merging
     *
     * When true (default), faces are only merged if all 4 vertices
     * have the same AO values. This preserves lighting detail.
     */
    void setRequireMatchingAO(bool required) {
        requireMatchingAO_ = required;
    }
    
private:
    /**
     * @brief Greedy merge faces in a 2D slice
     */
    void greedyMergeSlice(const std::vector<VisibleFace>& allFaces,
                          const std::vector<QuadAO>& ao,
                          const std::vector<size_t>& faceIndices,
                          FaceDirection faceDir,
                          int depth);
    
    /// Output merged quads
    std::vector<MergedQuad> quads_;
    
    /// Whether greedy merging is enabled
    bool greedyEnabled_ = true;
    
    /// Whether to require matching AO for merging
    bool requireMatchingAO_ = true;
    
    /// Temporary mask for greedy algorithm (reused)
    std::vector<int32_t> mask_;
    std::vector<uint8_t> aoMask_;
};

} // namespace voxel
} // namespace jupiter

