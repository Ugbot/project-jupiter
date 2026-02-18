#pragma once

/**
 * @file smooth_mesh_kernel.h
 * @brief Kernel for smooth terrain mesh generation
 *
 * Orchestrates Marching Cubes and Transvoxel meshing,
 * integrating with the voxel kernel system.
 */

#include "marching_cubes.h"
#include "transvoxel_mesher.h"
#include "smooth_vertex.h"
#include "mesh_mode.h"
#include "voxel_column.h"
#include "voxel_kernel.h"
#include "voxel_kernel_registry.h"
#include "face_culler.h"  // For FaceDirection enum

namespace jupiter {
namespace voxel {

/**
 * @brief Input for smooth mesh generation
 */
struct SmoothMeshInput {
    /// Chunk to mesh
    const ChunkColumns* chunk = nullptr;
    
    /// 6 neighbor chunks (+X, -X, +Y, -Y, +Z, -Z)
    const ChunkColumns* neighbors[6] = {nullptr};
    
    /// Chunk coordinate
    ChunkCoord coord;
    
    /// Mesh configuration
    MeshConfig config;
    
    /// LOD levels of neighbors (for transition cells)
    LODLevel neighborLODs[6] = {LODLevel::Full};
};

/**
 * @brief Smooth mesh kernel
 *
 * Generates smooth terrain meshes using Marching Cubes,
 * with Transvoxel transition cells at LOD boundaries.
 */
class SmoothMeshKernel {
public:
    SmoothMeshKernel();
    ~SmoothMeshKernel() = default;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Set default mesh configuration
     */
    void setDefaultConfig(const MeshConfig& config) {
        defaultConfig_ = config;
    }
    
    // ========================================================================
    // Execution
    // ========================================================================
    
    /**
     * @brief Execute smooth mesh generation for a chunk
     *
     * @param input Mesh input data
     * @param output Output mesh buffer
     */
    void execute(const SmoothMeshInput& input, SmoothMeshBuffer& output);
    
    /**
     * @brief Execute with explicit components
     */
    void execute(const ChunkColumns& chunk,
                 const ChunkColumns* neighbors[6],
                 const ChunkCoord& coord,
                 const MeshConfig& config,
                 SmoothMeshBuffer& output);
    
    /**
     * @brief Generate only regular cells (no transitions)
     */
    void generateRegularCells(const ChunkColumns& chunk,
                              const ChunkColumns* neighbors[6],
                              const ChunkCoord& coord,
                              const MeshConfig& config,
                              SmoothMeshBuffer& output);
    
    /**
     * @brief Generate transition cells for a specific face
     */
    void generateTransitionCells(const ChunkColumns& highResChunk,
                                  const ChunkColumns& lowResChunk,
                                  FaceDirection face,
                                  const ChunkCoord& coord,
                                  SmoothMeshBuffer& output);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    uint32_t getRegularCellsProcessed() const { return mcMesher_.getCellsProcessed(); }
    uint32_t getTransitionCellsProcessed() const { return tvMesher_.getCellsProcessed(); }
    uint32_t getTotalTriangles() const {
        return mcMesher_.getTrianglesGenerated() + tvMesher_.getTrianglesGenerated();
    }
    
private:
    /**
     * @brief Check if a face needs transition cells
     */
    bool needsTransition(LODLevel chunkLOD, LODLevel neighborLOD) const;
    
    /// Marching Cubes mesher
    MarchingCubesMesher mcMesher_;
    
    /// Transvoxel transition mesher
    TransvoxelMesher tvMesher_;
    
    /// Default configuration
    MeshConfig defaultConfig_;
};

/**
 * @brief Register the smooth mesh kernel with the registry
 */
void registerSmoothMeshKernel(VoxelKernelRegistry& registry);

} // namespace voxel
} // namespace jupiter

