#pragma once

/**
 * @file voxel_kernels.h
 * @brief Built-in voxel kernels
 *
 * Kernel implementations for terrain generation, CSG, and meshing.
 */

#include "voxel_kernel.h"
#include "voxel_kernel_registry.h"
#include "voxel_column.h"
#include "mesh_buffer.h"

namespace jupiter {
namespace voxel {
namespace kernels {

// ============================================================================
// Kernel Registration
// ============================================================================

/**
 * @brief Register all built-in voxel kernels
 */
void registerBuiltinKernels();

// ============================================================================
// Terrain Generation Kernels
// ============================================================================

/**
 * @brief Terrain noise kernel
 *
 * Generates procedural terrain using noise functions.
 * Input: seed, chunk coord
 * Output: Block column data
 */
VoxelStatus terrainNoiseKernel(const VoxelExecBatch* input,
                               VoxelExecBatch* output,
                               const VoxelKernelContext* ctx);

/**
 * @brief Flat terrain kernel
 *
 * Generates flat terrain at a specified height.
 */
VoxelStatus terrainFlatKernel(const VoxelExecBatch* input,
                              VoxelExecBatch* output,
                              const VoxelKernelContext* ctx);

// ============================================================================
// CSG Kernels
// ============================================================================

/**
 * @brief CSG apply kernel
 *
 * Applies a CSG primitive to chunk data.
 * Input: CSG primitive, block data
 * Output: Modified block data
 */
VoxelStatus csgApplyKernel(const VoxelExecBatch* input,
                           VoxelExecBatch* output,
                           const VoxelKernelContext* ctx);

// ============================================================================
// Meshing Kernels
// ============================================================================

/**
 * @brief Face culling kernel
 *
 * Determines which faces are visible (adjacent to air).
 * Input: Block data, neighbor chunks
 * Output: Visible face list
 */
VoxelStatus faceCullKernel(const VoxelExecBatch* input,
                           VoxelExecBatch* output,
                           const VoxelKernelContext* ctx);

/**
 * @brief AO calculation kernel
 *
 * Calculates ambient occlusion for visible face vertices.
 * Input: Block data, visible faces
 * Output: Per-vertex AO values
 */
VoxelStatus aoCalculateKernel(const VoxelExecBatch* input,
                              VoxelExecBatch* output,
                              const VoxelKernelContext* ctx);

/**
 * @brief Greedy meshing kernel
 *
 * Merges adjacent coplanar faces into larger quads.
 * Input: Visible faces, AO values
 * Output: Merged quads
 */
VoxelStatus greedyMeshKernel(const VoxelExecBatch* input,
                             VoxelExecBatch* output,
                             const VoxelKernelContext* ctx);

/**
 * @brief Vertex encoding kernel
 *
 * Encodes merged quads into GPU vertex format.
 * Input: Merged quads
 * Output: VoxelVertexGPU array
 */
VoxelStatus vertexEncodeKernel(const VoxelExecBatch* input,
                               VoxelExecBatch* output,
                               const VoxelKernelContext* ctx);

/**
 * @brief Complete mesh chunk kernel
 *
 * Orchestrates the full meshing pipeline:
 * 1. Face culling
 * 2. AO calculation
 * 3. Greedy meshing
 * 4. Vertex encoding
 *
 * Input: Block column data
 * Output: KernelMeshBuffer with GPU-ready vertices
 */
VoxelStatus meshChunkKernel(const VoxelExecBatch* input,
                            VoxelExecBatch* output,
                            const VoxelKernelContext* ctx);

// ============================================================================
// Kernel Definitions
// ============================================================================

/**
 * @brief Get the mesh chunk kernel definition
 */
inline VoxelKernel getMeshChunkKernel() {
    return VoxelKernelBuilder("mesh_chunk")
        .exec(meshChunkKernel)
        .inputs(VoxelColumnId::Blocks)
        .outputs(VoxelColumnId::MeshBuffer)
        .mode(VoxelKernelMode::ReadOnly)
        .batchSize(1)
        .priority(0)
        .build();
}

/**
 * @brief Get the terrain generation kernel definition
 */
inline VoxelKernel getTerrainNoiseKernel() {
    return VoxelKernelBuilder("terrain_noise")
        .exec(terrainNoiseKernel)
        .inputs(VoxelColumnId::None)
        .outputs(VoxelColumnId::Blocks)
        .mode(VoxelKernelMode::ReadWrite)
        .batchSize(1)
        .priority(-10)  // Run before meshing
        .build();
}

/**
 * @brief Get the CSG apply kernel definition
 */
inline VoxelKernel getCSGApplyKernel() {
    return VoxelKernelBuilder("csg_apply")
        .exec(csgApplyKernel)
        .inputs(VoxelColumnId::Blocks)
        .outputs(VoxelColumnId::Blocks)
        .mode(VoxelKernelMode::ReadWrite)
        .batchSize(1)
        .priority(-5)  // Run after terrain, before meshing
        .build();
}

} // namespace kernels
} // namespace voxel
} // namespace jupiter

