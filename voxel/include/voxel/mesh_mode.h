#pragma once

/**
 * @file mesh_mode.h
 * @brief Mesh mode selection for voxel rendering
 *
 * Supports blocky (Minecraft-style), smooth (Marching Cubes/Transvoxel),
 * and hybrid modes.
 */

#include <cstdint>

namespace jupiter {
namespace voxel {

/**
 * @brief Mesh generation mode
 */
enum class MeshMode : uint8_t {
    /// Blocky voxels (FaceCuller + GreedyMesh)
    /// Fast, Minecraft-style rendering
    Blocky = 0,
    
    /// Smooth terrain (Marching Cubes + Transvoxel)
    /// Uses SDF density for surface interpolation
    Smooth = 1,
    
    /// Hybrid: Smooth terrain with blocky placed blocks
    /// Natural terrain is smooth, player-placed blocks are blocky
    Hybrid = 2,
    
    /// Dual Contouring (future)
    /// Sharp features preserved
    DualContour = 3,
};

/**
 * @brief LOD level for terrain
 */
enum class LODLevel : uint8_t {
    Full = 0,       ///< Full resolution
    Half = 1,       ///< Half resolution (2x2x2 = 1)
    Quarter = 2,    ///< Quarter resolution (4x4x4 = 1)
    Eighth = 3,     ///< Eighth resolution (8x8x8 = 1)
};

/**
 * @brief Get voxel stride for LOD level
 */
inline int getLODStride(LODLevel lod) {
    return 1 << static_cast<int>(lod);
}

/**
 * @brief Mesh configuration
 */
struct MeshConfig {
    /// Primary mesh mode
    MeshMode mode = MeshMode::Blocky;
    
    /// LOD level for this chunk
    LODLevel lod = LODLevel::Full;
    
    /// Enable greedy meshing for blocky mode
    bool greedyMeshing = true;
    
    /// Enable ambient occlusion
    bool ambientOcclusion = true;
    
    /// Generate transition cells for LOD boundaries
    bool generateTransitions = true;
    
    /// Surface threshold for smooth meshing (typically 0.0)
    float isoLevel = 0.0f;
    
    /// Interpolation sharpness for smooth terrain
    float interpolationSharpness = 1.0f;
};

/**
 * @brief Check if mode uses density/SDF
 */
inline bool usesDensity(MeshMode mode) {
    return mode == MeshMode::Smooth ||
           mode == MeshMode::Hybrid ||
           mode == MeshMode::DualContour;
}

/**
 * @brief Check if mode uses blocky meshing
 */
inline bool usesBlocky(MeshMode mode) {
    return mode == MeshMode::Blocky || mode == MeshMode::Hybrid;
}

/**
 * @brief Check if mode needs transition cells
 */
inline bool needsTransitions(MeshMode mode) {
    return mode == MeshMode::Smooth ||
           mode == MeshMode::Hybrid ||
           mode == MeshMode::DualContour;
}

} // namespace voxel
} // namespace jupiter



