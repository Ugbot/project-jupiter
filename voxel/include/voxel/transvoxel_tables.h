#pragma once

/**
 * @file transvoxel_tables.h
 * @brief Transvoxel Algorithm lookup tables
 *
 * Based on Eric Lengyel's Transvoxel Algorithm.
 * See: https://transvoxel.org/ and https://github.com/EricLengyel/Transvoxel
 *
 * The Transvoxel Algorithm is used to seamlessly stitch together triangle
 * meshes generated at different resolutions for LOD (Level of Detail).
 *
 * MIT License
 */

#include <cstdint>

namespace jupiter {
namespace voxel {
namespace transvoxel {

// ============================================================================
// Regular Cell Data (Marching Cubes)
// ============================================================================

/**
 * @brief Regular cell data for Marching Cubes triangulation
 *
 * Each regular cell has 8 corners sampled at the same resolution.
 * There are 256 possible cases (2^8 corner states).
 */
struct RegularCellData {
    uint8_t geometryCounts;     ///< High nibble = vertex count, low nibble = triangle count
    uint8_t vertexIndex[15];    ///< Indices into edges for each vertex
    
    int getVertexCount() const { return geometryCounts >> 4; }
    int getTriangleCount() const { return geometryCounts & 0x0F; }
};

/**
 * @brief Maps each of the 256 regular cell cases to one of 16 equivalence classes
 */
extern const uint8_t regularCellClass[256];

/**
 * @brief Triangulation data for the 16 regular cell equivalence classes
 */
extern const RegularCellData regularCellData[16];

/**
 * @brief Vertex data for each edge of the 256 regular cell cases
 *
 * For each case, up to 12 edges can have vertices.
 * High byte = corner indices for edge endpoints
 * Low byte = reuse info
 */
extern const uint16_t regularVertexData[256][12];

// ============================================================================
// Transition Cell Data
// ============================================================================

/**
 * @brief Transition cell data for LOD boundary stitching
 *
 * Transition cells connect high-resolution and low-resolution meshes.
 * There are 512 possible cases (9 samples: 2^9).
 */
struct TransitionCellData {
    uint8_t geometryCounts;     ///< High nibble = vertex count, low nibble = triangle count
    uint8_t vertexIndex[36];    ///< Indices for each vertex (up to 12 triangles)
    
    int getVertexCount() const { return geometryCounts >> 4; }
    int getTriangleCount() const { return geometryCounts & 0x0F; }
};

/**
 * @brief Maps each of the 512 transition cell cases to one of 56 equivalence classes
 *
 * High bit indicates whether the triangulation should be inverted.
 */
extern const uint8_t transitionCellClass[512];

/**
 * @brief Triangulation data for the 56 transition cell equivalence classes
 */
extern const TransitionCellData transitionCellData[56];

/**
 * @brief Corner mapping for transition cells
 *
 * Maps the 13 sample points (9 face + 4 cell corners) to positions.
 */
extern const uint8_t transitionCornerData[13];

/**
 * @brief Vertex data for transition cell edges
 *
 * For each of 512 cases, defines how to construct vertices.
 */
extern const uint16_t transitionVertexData[512][12];

// ============================================================================
// Edge Data
// ============================================================================

/**
 * @brief Edge endpoint indices for regular cells
 *
 * Each edge connects two of the 8 cell corners.
 * edgeEndpoints[edge] = (corner0, corner1)
 */
extern const uint8_t regularEdgeEndpoints[12][2];

/**
 * @brief Edge endpoint indices for transition cells
 */
extern const uint8_t transitionEdgeEndpoints[10][2];

// ============================================================================
// Corner Positions
// ============================================================================

/**
 * @brief Local positions of the 8 corners of a regular cell
 *
 * Indexed as: corner = x + y*2 + z*4
 */
extern const int8_t regularCornerPositions[8][3];

/**
 * @brief Local positions of the 9 transition cell face samples
 *
 * The 9 samples on the high-resolution face:
 * 0-3: corners, 4-7: edge midpoints, 8: center
 */
extern const int8_t transitionFacePositions[9][2];

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Get the cell class for a regular cell case
 *
 * @param caseIndex 8-bit corner mask (0-255)
 * @return Equivalence class (0-15)
 */
inline uint8_t getRegularCellClass(uint8_t caseIndex) {
    return regularCellClass[caseIndex];
}

/**
 * @brief Get the cell class for a transition cell case
 *
 * @param caseIndex 9-bit corner mask (0-511)
 * @return Equivalence class (low 7 bits) and invert flag (high bit)
 */
inline uint8_t getTransitionCellClass(uint16_t caseIndex) {
    return transitionCellClass[caseIndex];
}

/**
 * @brief Check if transition cell triangulation should be inverted
 */
inline bool isTransitionInverted(uint8_t cellClass) {
    return (cellClass & 0x80) != 0;
}

/**
 * @brief Get the actual class index (without invert flag)
 */
inline uint8_t getTransitionClassIndex(uint8_t cellClass) {
    return cellClass & 0x7F;
}

} // namespace transvoxel
} // namespace voxel
} // namespace jupiter



