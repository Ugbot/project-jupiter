/**
 * @file voxel_kernels.cpp
 * @brief Implementation of built-in voxel kernels
 */

#include <voxel/voxel_kernels.h>
#include <voxel/face_culler.h>
#include <voxel/ao_calculator.h>
#include <voxel/greedy_mesher.h>
#include <voxel/vertex_encoder.h>
#include <voxel/csg_evaluator.h>
#include <cmath>

namespace jupiter {
namespace voxel {
namespace kernels {

// ============================================================================
// Kernel Registration
// ============================================================================

void registerBuiltinKernels() {
    auto& registry = VoxelKernelRegistry::instance();
    
    // Terrain kernels
    registry.registerKernel(getTerrainNoiseKernel());
    registry.registerKernel(
        VoxelKernelBuilder("terrain_flat")
            .exec(terrainFlatKernel)
            .inputs(VoxelColumnId::None)
            .outputs(VoxelColumnId::Blocks)
            .mode(VoxelKernelMode::ReadWrite)
            .batchSize(1)
            .build()
    );
    
    // CSG kernels
    registry.registerKernel(getCSGApplyKernel());
    
    // Meshing pipeline kernels
    registry.registerKernel(
        VoxelKernelBuilder("face_cull")
            .exec(faceCullKernel)
            .inputs(VoxelColumnId::Blocks | VoxelColumnId::Neighbors)
            .outputs(VoxelColumnId::VisibleFaces)
            .readOnly()
            .batchSize(1)
            .build()
    );
    
    registry.registerKernel(
        VoxelKernelBuilder("ao_calculate")
            .exec(aoCalculateKernel)
            .inputs(VoxelColumnId::Blocks | VoxelColumnId::VisibleFaces)
            .outputs(VoxelColumnId::VertexAO)
            .readOnly()
            .batchSize(1)
            .build()
    );
    
    registry.registerKernel(
        VoxelKernelBuilder("greedy_mesh")
            .exec(greedyMeshKernel)
            .inputs(VoxelColumnId::VisibleFaces | VoxelColumnId::VertexAO)
            .outputs(VoxelColumnId::MergedQuads)
            .readOnly()
            .batchSize(1)
            .build()
    );
    
    registry.registerKernel(
        VoxelKernelBuilder("vertex_encode")
            .exec(vertexEncodeKernel)
            .inputs(VoxelColumnId::MergedQuads)
            .outputs(VoxelColumnId::MeshBuffer)
            .readOnly()
            .batchSize(1)
            .build()
    );
    
    // Complete mesh pipeline
    registry.registerKernel(getMeshChunkKernel());
}

// ============================================================================
// Terrain Generation Kernels
// ============================================================================

// Simple hash function for noise
static uint32_t hash(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h ^= static_cast<uint32_t>(z) * 1274126177u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h;
}

// Simple 2D noise for heightmap
static float noise2D(float x, float z, uint32_t seed) {
    int32_t ix = static_cast<int32_t>(std::floor(x));
    int32_t iz = static_cast<int32_t>(std::floor(z));
    float fx = x - ix;
    float fz = z - iz;
    
    // Smooth interpolation
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    
    // Hash corner values
    float h00 = static_cast<float>(hash(ix, 0, iz, seed) & 0xFFFF) / 65535.0f;
    float h10 = static_cast<float>(hash(ix + 1, 0, iz, seed) & 0xFFFF) / 65535.0f;
    float h01 = static_cast<float>(hash(ix, 0, iz + 1, seed) & 0xFFFF) / 65535.0f;
    float h11 = static_cast<float>(hash(ix + 1, 0, iz + 1, seed) & 0xFFFF) / 65535.0f;
    
    // Bilinear interpolation
    float h0 = h00 + fx * (h10 - h00);
    float h1 = h01 + fx * (h11 - h01);
    
    return h0 + fz * (h1 - h0);
}

// Multi-octave noise
static float fbm(float x, float z, uint32_t seed, int octaves = 4) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        value += noise2D(x * frequency, z * frequency, seed + i) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    
    return value / maxValue;
}

VoxelStatus terrainNoiseKernel(const VoxelExecBatch* input,
                               VoxelExecBatch* output,
                               const VoxelKernelContext* ctx) {
    if (!output || !ctx) {
        return VoxelStatus::InvalidInput;
    }
    
    ChunkColumns* chunk = output->column<ChunkColumns>(VoxelColumnId::Blocks);
    if (!chunk) {
        return VoxelStatus::InvalidInput;
    }
    
    const ChunkCoord& coord = output->chunkCoord;
    uint32_t seed = ctx->seed;
    
    // Base terrain parameters
    const float baseHeight = 64.0f;
    const float heightScale = 32.0f;
    const float noiseScale = 0.02f;
    
    // World offset
    float worldX = static_cast<float>(coord.x * CHUNK_SIZE);
    float worldZ = static_cast<float>(coord.z * CHUNK_SIZE);
    
    // Generate terrain for each column
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            float wx = worldX + lx;
            float wz = worldZ + lz;
            
            // Get height from noise
            float n = fbm(wx * noiseScale, wz * noiseScale, seed);
            int height = static_cast<int>(baseHeight + n * heightScale);
            height = std::max(1, std::min(height, CHUNK_HEIGHT - 1));
            
            VoxelColumn& col = chunk->at(lx, lz);
            
            // Fill column
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                if (y == 0) {
                    col.setBlock(y, BLOCK_STONE);  // Bottom layer
                } else if (y < height - 4) {
                    col.setBlock(y, BLOCK_STONE);
                } else if (y < height) {
                    col.setBlock(y, BLOCK_DIRT);
                } else if (y == height) {
                    col.setBlock(y, BLOCK_GRASS);
                } else {
                    col.setBlock(y, BLOCK_AIR);
                }
            }
            
            col.updateBounds();
        }
    }
    
    chunk->incrementGeneration();
    return VoxelStatus::Ok;
}

VoxelStatus terrainFlatKernel(const VoxelExecBatch* input,
                              VoxelExecBatch* output,
                              const VoxelKernelContext* ctx) {
    (void)input;
    
    if (!output) {
        return VoxelStatus::InvalidInput;
    }
    
    ChunkColumns* chunk = output->column<ChunkColumns>(VoxelColumnId::Blocks);
    if (!chunk) {
        return VoxelStatus::InvalidInput;
    }
    
    // Default flat height
    const int flatHeight = 64;
    
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            VoxelColumn& col = chunk->at(lx, lz);
            
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                if (y == 0) {
                    col.setBlock(y, BLOCK_STONE);  // Bottom layer
                } else if (y < flatHeight - 3) {
                    col.setBlock(y, BLOCK_STONE);
                } else if (y < flatHeight) {
                    col.setBlock(y, BLOCK_DIRT);
                } else if (y == flatHeight) {
                    col.setBlock(y, BLOCK_GRASS);
                } else {
                    col.setBlock(y, BLOCK_AIR);
                }
            }
            
            col.updateBounds();
        }
    }
    
    chunk->incrementGeneration();
    return VoxelStatus::Ok;
}

// ============================================================================
// CSG Kernels
// ============================================================================

VoxelStatus csgApplyKernel(const VoxelExecBatch* input,
                           VoxelExecBatch* output,
                           const VoxelKernelContext* ctx) {
    if (!input || !output || !ctx) {
        return VoxelStatus::InvalidInput;
    }
    
    // Get CSG primitive from user data
    const CSGPrimitive* primitive = static_cast<const CSGPrimitive*>(input->userData);
    if (!primitive) {
        return VoxelStatus::InvalidInput;
    }
    
    ChunkColumns* chunk = output->column<ChunkColumns>(VoxelColumnId::Blocks);
    if (!chunk) {
        return VoxelStatus::InvalidInput;
    }
    
    // Apply CSG directly to ChunkColumns (no copy needed)
    CSGEvaluator evaluator;
    evaluator.evaluate(*primitive, *chunk, output->chunkCoord);
    
    return VoxelStatus::Ok;
}

// ============================================================================
// Meshing Kernels
// ============================================================================

VoxelStatus faceCullKernel(const VoxelExecBatch* input,
                           VoxelExecBatch* output,
                           const VoxelKernelContext* ctx) {
    if (!input || !output) {
        return VoxelStatus::InvalidInput;
    }
    
    const ChunkColumns* chunk = input->column<ChunkColumns>(VoxelColumnId::Blocks);
    if (!chunk) {
        return VoxelStatus::InvalidInput;
    }
    
    // Get output visible faces
    auto* faces = output->column<std::vector<VisibleFace>>(VoxelColumnId::VisibleFaces);
    if (!faces) {
        return VoxelStatus::InvalidInput;
    }
    
    // Get neighbor chunks from context
    const ChunkColumns* neighbors[6] = {};
    if (ctx) {
        for (int i = 0; i < 6; ++i) {
            neighbors[i] = ctx->neighborChunks[i];
        }
    }
    
    FaceCuller culler;
    culler.process(*chunk, neighbors);
    
    *faces = std::move(culler.visibleFaces());
    
    return VoxelStatus::Ok;
}

VoxelStatus aoCalculateKernel(const VoxelExecBatch* input,
                              VoxelExecBatch* output,
                              const VoxelKernelContext* ctx) {
    if (!input || !output) {
        return VoxelStatus::InvalidInput;
    }
    
    const ChunkColumns* chunk = input->column<ChunkColumns>(VoxelColumnId::Blocks);
    const auto* faces = input->column<std::vector<VisibleFace>>(VoxelColumnId::VisibleFaces);
    
    if (!chunk || !faces) {
        return VoxelStatus::InvalidInput;
    }
    
    auto* aoOut = output->column<std::vector<QuadAO>>(VoxelColumnId::VertexAO);
    if (!aoOut) {
        return VoxelStatus::InvalidInput;
    }
    
    const ChunkColumns* neighbors[6] = {};
    if (ctx) {
        for (int i = 0; i < 6; ++i) {
            neighbors[i] = ctx->neighborChunks[i];
        }
    }
    
    AOCalculator ao;
    ao.process(*chunk, neighbors, *faces);
    
    *aoOut = std::move(ao.quadAO());
    
    return VoxelStatus::Ok;
}

VoxelStatus greedyMeshKernel(const VoxelExecBatch* input,
                             VoxelExecBatch* output,
                             const VoxelKernelContext* ctx) {
    (void)ctx;
    
    if (!input || !output) {
        return VoxelStatus::InvalidInput;
    }
    
    const auto* faces = input->column<std::vector<VisibleFace>>(VoxelColumnId::VisibleFaces);
    const auto* ao = input->column<std::vector<QuadAO>>(VoxelColumnId::VertexAO);
    
    if (!faces) {
        return VoxelStatus::InvalidInput;
    }
    
    auto* quads = output->column<std::vector<MergedQuad>>(VoxelColumnId::MergedQuads);
    if (!quads) {
        return VoxelStatus::InvalidInput;
    }
    
    GreedyMesher mesher;
    
    if (ao && !ao->empty()) {
        mesher.process(*faces, *ao);
    } else {
        mesher.processNoAO(*faces);
    }
    
    *quads = std::move(mesher.quads());
    
    return VoxelStatus::Ok;
}

VoxelStatus vertexEncodeKernel(const VoxelExecBatch* input,
                               VoxelExecBatch* output,
                               const VoxelKernelContext* ctx) {
    (void)ctx;
    
    if (!input || !output) {
        return VoxelStatus::InvalidInput;
    }
    
    const auto* quads = input->column<std::vector<MergedQuad>>(VoxelColumnId::MergedQuads);
    if (!quads) {
        return VoxelStatus::InvalidInput;
    }
    
    auto* meshBuf = output->column<KernelMeshBuffer>(VoxelColumnId::MeshBuffer);
    if (!meshBuf) {
        return VoxelStatus::InvalidInput;
    }
    
    VertexEncoder encoder;
    encoder.encode(*quads, output->chunkCoord);
    
    meshBuf->vertices = std::move(encoder.vertices());
    meshBuf->quadCount = encoder.quadCount();
    meshBuf->chunkCoord = output->chunkCoord;
    
    // Store flip flags for index generation
    meshBuf->flipFlags.clear();
    meshBuf->flipFlags.reserve(quads->size());
    for (const auto& q : *quads) {
        meshBuf->flipFlags.push_back(q.ao.shouldFlip());
    }
    
    return VoxelStatus::Ok;
}

VoxelStatus meshChunkKernel(const VoxelExecBatch* input,
                            VoxelExecBatch* output,
                            const VoxelKernelContext* ctx) {
    if (!input || !output || !ctx) {
        return VoxelStatus::InvalidInput;
    }
    
    const ChunkColumns* chunk = input->column<ChunkColumns>(VoxelColumnId::Blocks);
    if (!chunk) {
        return VoxelStatus::InvalidInput;
    }
    
    KernelMeshBuffer* meshBuf = output->column<KernelMeshBuffer>(VoxelColumnId::MeshBuffer);
    if (!meshBuf) {
        return VoxelStatus::InvalidInput;
    }
    
    // Get neighbor chunks
    const ChunkColumns* neighbors[6] = {};
    for (int i = 0; i < 6; ++i) {
        neighbors[i] = ctx->neighborChunks[i];
    }
    
    // Stage 1: Face culling
    FaceCuller culler;
    culler.process(*chunk, neighbors);
    
    if (culler.faceCount() == 0) {
        meshBuf->clear();
        return VoxelStatus::Ok;
    }
    
    // Stage 2: AO calculation
    AOCalculator ao;
    ao.process(*chunk, neighbors, culler.visibleFaces());
    
    // Stage 3: Greedy meshing
    GreedyMesher mesher;
    mesher.process(culler.visibleFaces(), ao.quadAO());
    
    // Stage 4: Vertex encoding
    VertexEncoder encoder;
    encoder.encode(mesher.quads(), output->chunkCoord);
    
    // Fill output buffer
    meshBuf->vertices = std::move(encoder.vertices());
    meshBuf->quadCount = encoder.quadCount();
    meshBuf->chunkCoord = output->chunkCoord;
    meshBuf->generation = chunk->getEditGeneration();
    
    // Store flip flags
    meshBuf->flipFlags.clear();
    meshBuf->flipFlags.reserve(mesher.quadCount());
    for (const auto& q : mesher.quads()) {
        meshBuf->flipFlags.push_back(q.ao.shouldFlip());
    }
    
    return VoxelStatus::Ok;
}

} // namespace kernels
} // namespace voxel
} // namespace jupiter

