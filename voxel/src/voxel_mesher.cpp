/**
 * @file voxel_mesher.cpp
 * @brief Implementation of VoxelMesher using stb_voxel_render
 */

// Configure stb_voxel_render before including
#define STBVOX_CONFIG_MODE (30)        // Mode 30: 8-byte vertices (compact)
#define STBVOX_CONFIG_PRECISION_Z (0)  // Full Z precision

// Define implementation in this translation unit
#define STB_VOXEL_RENDER_IMPLEMENTATION
#include <stb_voxel_render.h>

#include <voxel/voxel_mesher.h>
#include <cstring>
#include <cmath>

namespace jupiter {
namespace voxel {

// ============================================================================
// VoxelMesher Implementation
// ============================================================================

VoxelMesher::VoxelMesher() {
    // Mesh maker is heap-allocated to avoid stack issues
    meshMaker_ = new stbvox_mesh_maker();
}

VoxelMesher::~VoxelMesher() {
    if (meshMaker_) {
        delete static_cast<stbvox_mesh_maker*>(meshMaker_);
        meshMaker_ = nullptr;
    }
}

VoxelMesher::VoxelMesher(VoxelMesher&& other) noexcept
    : meshMaker_(other.meshMaker_)
    , currentBuffer_(other.currentBuffer_)
    , currentBufferSize_(other.currentBufferSize_)
    , currentChunk_(other.currentChunk_)
    , initialized_(other.initialized_)
{
    other.meshMaker_ = nullptr;
    other.currentBuffer_ = nullptr;
    other.currentBufferSize_ = 0;
    other.initialized_ = false;
}

VoxelMesher& VoxelMesher::operator=(VoxelMesher&& other) noexcept {
    if (this != &other) {
        if (meshMaker_) {
            delete static_cast<stbvox_mesh_maker*>(meshMaker_);
        }
        meshMaker_ = other.meshMaker_;
        currentBuffer_ = other.currentBuffer_;
        currentBufferSize_ = other.currentBufferSize_;
        currentChunk_ = other.currentChunk_;
        initialized_ = other.initialized_;

        other.meshMaker_ = nullptr;
        other.currentBuffer_ = nullptr;
        other.currentBufferSize_ = 0;
        other.initialized_ = false;
    }
    return *this;
}

void VoxelMesher::initialize() {
    if (initialized_) return;

    auto* maker = static_cast<stbvox_mesh_maker*>(meshMaker_);
    stbvox_init_mesh_maker(maker);
    stbvox_set_default_mesh(maker, 0);

    initialized_ = true;
}

void VoxelMesher::shutdown() {
    currentBuffer_ = nullptr;
    currentBufferSize_ = 0;
    initialized_ = false;
}

void VoxelMesher::setBuffer(uint8_t* buffer, size_t bufferSize) {
    currentBuffer_ = buffer;
    currentBufferSize_ = bufferSize;
}

void VoxelMesher::setBuffer(MeshBuffer* meshBuffer) {
    if (meshBuffer) {
        currentBuffer_ = meshBuffer->data;
        currentBufferSize_ = MeshBuffer::BUFFER_SIZE;
    } else {
        currentBuffer_ = nullptr;
        currentBufferSize_ = 0;
    }
}

void VoxelMesher::beginChunk(const ChunkVoxelData* chunk,
                            const ChunkVoxelData* neighbors[6],
                            const ChunkCoord& chunkCoord)
{
    if (!initialized_ || !chunk || !currentBuffer_) return;

    currentChunk_ = chunkCoord;

    auto* maker = static_cast<stbvox_mesh_maker*>(meshMaker_);

    // Reset buffers for new chunk
    stbvox_reset_buffers(maker);
    stbvox_set_buffer(maker, 0, 0, currentBuffer_, currentBufferSize_);

    // Calculate strides for the padded array
    // Memory layout: Z varies fastest (stride 1), then Y (stride PADDED_SIZE), then X (stride PADDED_HEIGHT*PADDED_SIZE)
    // stb_voxel_render REQUIRES Z to be consecutive (stride 1)
    const int strideX = PADDED_HEIGHT * PADDED_SIZE;  // 130 * 18 = 2340
    const int strideY = PADDED_SIZE;                   // 18
    stbvox_set_input_stride(maker, strideX, strideY);

    // Set input range (skip the border voxels for actual mesh generation)
    // The borders contain neighbor data for correct face culling
    stbvox_set_input_range(maker,
        CHUNK_BORDER, CHUNK_BORDER, CHUNK_BORDER,  // Start (skip border)
        CHUNK_BORDER + CHUNK_SIZE,                  // End X
        CHUNK_BORDER + CHUNK_HEIGHT,                // End Y (tall chunks)
        CHUNK_BORDER + CHUNK_SIZE);                 // End Z

    // Set up input description
    stbvox_input_description* desc = stbvox_get_input_description(maker);

    // Block types serve as both geometry and color index
    // Cast away const - stb API is non-const but doesn't modify the data
    desc->blocktype = const_cast<uint8_t*>(chunk->blocks);
    desc->color = const_cast<uint8_t*>(chunk->blocks);  // Use block type as color palette index

    // Set lighting input for ambient occlusion
    // stb_voxel_render averages lighting values at vertices from adjacent blocks.
    // Solid blocks = 63 (max), air blocks = 0 creates Minecraft-style AO.
    // We use the chunk's lighting array which mirrors the block data.
    desc->lighting = const_cast<uint8_t*>(chunk->lighting);
}

MeshResult VoxelMesher::meshify() {
    MeshResult result;

    if (!initialized_ || !currentBuffer_) {
        result.volumeDone = true;
        return result;
    }

    auto* maker = static_cast<stbvox_mesh_maker*>(meshMaker_);

    // Generate mesh
    int res = stbvox_make_mesh(maker);

    // Get results
    result.numQuads = stbvox_get_quad_count(maker, 0);
    result.numVertices = result.numQuads * 4;
    result.numBytes = result.numVertices * STB_VERTEX_SIZE;
    result.vertices = currentBuffer_;

    // Get transform
    float transform[3][3];
    stbvox_get_transform(maker, transform);

    result.scale = glm::vec3(transform[0][0], transform[0][1], transform[0][2]);
    result.translate = glm::vec3(transform[1][0], transform[1][1], transform[1][2]);
    result.texTranslate = glm::vec3(transform[2][0], transform[2][1], transform[2][2]);

    // Calculate AABB
    result.aabbMin = currentChunk_.toWorldPos();
    result.aabbMax = result.aabbMin + glm::vec3(CHUNK_SIZE);

    if (res == 0) {
        // Buffer full, need to call again
        result.bufferFull = true;
        result.volumeDone = false;

        // Reset buffer for next call
        stbvox_reset_buffers(maker);
        stbvox_set_buffer(maker, 0, 0, currentBuffer_, currentBufferSize_);
    } else {
        // Volume complete
        result.volumeDone = true;
        result.bufferFull = false;
    }

    return result;
}

VoxelVertex VoxelMesher::convertVertex(const StbVoxelVertex& stbVertex,
                                       const ChunkCoord& chunkCoord)
{
    VoxelVertex out;

    // Mode 30 vertex format (reverse engineered from stb_voxel_render):
    // attr_vertex:
    //   bits 0-6:   X position (0-127)
    //   bits 7-13:  Y position (0-127)
    //   bits 14-22: Z position (0-511)
    //   bits 23-28: Ambient occlusion (0-63)
    //   bits 29-31: Texture lerp (0-7)
    //
    // attr_face:
    //   bits 0-5:   Color index (0-63)
    //   bits 2-6:   Normal index (0-31, but only 0-5 used for cube faces)
    //   Other bits: tex1, tex2, rotation, etc.

    // Extract position (in voxel units within chunk)
    uint32_t v = stbVertex.attr_vertex;
    float x = static_cast<float>(v & 0x7F);
    float y = static_cast<float>((v >> 7) & 0x7F);
    float z = static_cast<float>((v >> 14) & 0x1FF);

    // Extract AO
    uint32_t ao = (v >> 23) & 0x3F;

    // Extract normal index from attr_face
    uint32_t f = stbVertex.attr_face;
    uint32_t normalIndex = (f >> 2) & 0x1F;
    if (normalIndex > 5) normalIndex = 0;  // Clamp to valid range

    // Extract color/material index
    uint32_t colorIndex = f & 0x3F;

    // Encode position as unorm16 (0-65535 maps to 0-16)
    // stb outputs 0-127 range, we need to scale to 0-16 chunk range
    out.position[0] = static_cast<uint16_t>((x / 127.0f) * 16.0f * 65535.0f / 16.0f);
    out.position[1] = static_cast<uint16_t>((y / 127.0f) * 16.0f * 65535.0f / 16.0f);
    out.position[2] = static_cast<uint16_t>((z / 511.0f) * 16.0f * 65535.0f / 16.0f);

    // Pack normal (3 bits) and AO (5 bits)
    out.normalAO = static_cast<uint16_t>((normalIndex & 0x7) | ((ao & 0x1F) << 3));

    // Material index (use color index as material)
    out.materialIndex = static_cast<uint16_t>(colorIndex);

    // UV (simplified - generate from position projection)
    // For axis-aligned faces, UVs are just the other two axes
    out.packedUV = 0;  // Will be calculated in shader based on normal

    return out;
}

void VoxelMesher::convertMesh(const StbVoxelVertex* stbVertices,
                             uint32_t numVertices,
                             VoxelVertex* outVertices,
                             const ChunkCoord& chunkCoord,
                             const glm::vec3& scale)
{
    for (uint32_t i = 0; i < numVertices; ++i) {
        outVertices[i] = convertVertex(stbVertices[i], chunkCoord);
    }
}

// ============================================================================
// Terrain Generation
// ============================================================================

// Simple hash-based pseudo-random for procedural generation
static uint32_t hash(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h ^= static_cast<uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Simple 3D noise
static float noise3D(float x, float y, float z, uint32_t seed) {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));

    float fx = x - ix;
    float fy = y - iy;
    float fz = z - iz;

    // Smoothstep
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    fz = fz * fz * (3.0f - 2.0f * fz);

    // Hash corners
    auto corner = [seed](int x, int y, int z) {
        return static_cast<float>(hash(x, y, z, seed) & 0xFFFF) / 65535.0f;
    };

    float c000 = corner(ix, iy, iz);
    float c100 = corner(ix + 1, iy, iz);
    float c010 = corner(ix, iy + 1, iz);
    float c110 = corner(ix + 1, iy + 1, iz);
    float c001 = corner(ix, iy, iz + 1);
    float c101 = corner(ix + 1, iy, iz + 1);
    float c011 = corner(ix, iy + 1, iz + 1);
    float c111 = corner(ix + 1, iy + 1, iz + 1);

    // Trilinear interpolation
    float c00 = c000 * (1 - fx) + c100 * fx;
    float c10 = c010 * (1 - fx) + c110 * fx;
    float c01 = c001 * (1 - fx) + c101 * fx;
    float c11 = c011 * (1 - fx) + c111 * fx;

    float c0 = c00 * (1 - fy) + c10 * fy;
    float c1 = c01 * (1 - fy) + c11 * fy;

    return c0 * (1 - fz) + c1 * fz;
}

void generateProceduralTerrain(ChunkVoxelData* chunk,
                              const ChunkCoord& coord,
                              uint32_t seed)
{
    if (!chunk) return;

    chunk->clear();

    const float scale = 0.05f;  // Noise scale
    const float amplitude = 8.0f;  // Height variation
    const float baseHeight = 32.0f;  // Base terrain height (in world units)

    // World offset
    const int worldX = coord.x * CHUNK_SIZE;
    const int worldY = coord.y * CHUNK_SIZE;
    const int worldZ = coord.z * CHUNK_SIZE;

    // Y is UP in our coordinate system
    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                int wx = worldX + lx;
                int wy = worldY + ly;  // Y is vertical (height)
                int wz = worldZ + lz;

                // Generate height using 2D noise on XZ plane
                float n = noise3D(
                    static_cast<float>(wx) * scale,
                    static_cast<float>(wz) * scale,  // Use Z for horizontal noise
                    0.0f,
                    seed
                );

                int height = static_cast<int>(baseHeight + n * amplitude);

                BlockType block = BLOCK_AIR;
                uint8_t lighting = 63;  // Air = fully lit (for AO)

                // Y is the vertical axis
                if (wy < height - 3) {
                    block = BLOCK_STONE;
                    lighting = 0;  // Solid = occluded (for AO averaging)
                } else if (wy < height - 1) {
                    block = BLOCK_DIRT;
                    lighting = 0;
                } else if (wy < height) {
                    block = BLOCK_GRASS;
                    lighting = 0;
                }

                // Set block and lighting at same index
                int idx = ChunkVoxelData::getIndex(lx, ly, lz);
                chunk->blocks[idx] = block;
                chunk->lighting[idx] = lighting;
            }
        }
    }
    chunk->editGeneration++;
}

void generateFlatTerrain(ChunkVoxelData* chunk,
                        int groundHeight,
                        BlockType groundBlock)
{
    if (!chunk) return;

    chunk->clear();

    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                BlockType block = BLOCK_AIR;

                if (lz < groundHeight - 2) {
                    block = BLOCK_STONE;
                } else if (lz < groundHeight) {
                    block = BLOCK_DIRT;
                } else if (lz == groundHeight) {
                    block = groundBlock;
                }

                chunk->setBlock(lx, ly, lz, block);
            }
        }
    }
}

} // namespace voxel
} // namespace jupiter
