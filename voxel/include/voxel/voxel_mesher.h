#pragma once

#include "voxel_types.h"
#include "mesh_buffer_pool.h"
#include <glm/glm.hpp>

/**
 * @file voxel_mesher.h
 * @brief Wrapper around stb_voxel_render for chunk mesh generation
 *
 * Following Project Jupiter principles:
 * - Uses pooled buffers (no stack allocation)
 * - No runtime allocations during meshing
 * - Returns raw vertex data for GPU upload
 */

namespace jupiter {
namespace voxel {

/**
 * @brief Result of mesh generation
 */
struct MeshResult {
    bool volumeDone = false;      ///< True when entire volume is meshed
    bool bufferFull = false;      ///< True if output buffer filled (call again)

    const void* vertices = nullptr;  ///< Pointer to vertex data
    uint32_t numQuads = 0;           ///< Number of quads generated
    uint32_t numVertices = 0;        ///< Number of vertices (numQuads * 4)
    uint32_t numBytes = 0;           ///< Total bytes of vertex data

    glm::vec3 scale;              ///< Scale factor from stb_voxel_render
    glm::vec3 translate;          ///< Translation offset
    glm::vec3 texTranslate;       ///< Texture coordinate translation

    glm::vec3 aabbMin;            ///< Mesh bounding box min
    glm::vec3 aabbMax;            ///< Mesh bounding box max
};

/**
 * @brief stb_voxel_render Mode 30 vertex format (8 bytes per vertex)
 *
 * This is the raw format from stb_voxel_render. We convert this to
 * VoxelVertex (12 bytes) for our rendering pipeline.
 */
struct StbVoxelVertex {
    uint32_t attr_vertex;  ///< Packed position + AO + texlerp
    uint32_t attr_face;    ///< Face data (normal, color, tex)
};

static_assert(sizeof(StbVoxelVertex) == 8, "StbVoxelVertex must be 8 bytes");

/**
 * @brief Wrapper around stb_voxel_render for chunk mesh generation
 *
 * Uses Mode 30 which outputs 8-byte vertices. The mesher converts
 * this to our 12-byte VoxelVertex format for PBR rendering.
 *
 * IMPORTANT: This class uses pooled buffers. Call setBuffer() with a
 * buffer from MeshBufferPool before meshing.
 *
 * Thread-safety: Each thread should have its own VoxelMesher instance.
 */
class VoxelMesher {
public:
    /// Maximum quads per mesh (limits vertex buffer size)
    static constexpr uint32_t MAX_QUADS = MeshBuffer::MAX_QUADS;

    /// Maximum vertices (4 per quad)
    static constexpr uint32_t MAX_VERTICES = MeshBuffer::MAX_VERTICES;

    /// Bytes per stb vertex
    static constexpr uint32_t STB_VERTEX_SIZE = MeshBuffer::STB_VERTEX_SIZE;

    /// Output buffer size for stb_voxel_render
    static constexpr size_t STB_BUFFER_SIZE = MeshBuffer::BUFFER_SIZE;

    VoxelMesher();
    ~VoxelMesher();

    // Non-copyable (contains opaque state)
    VoxelMesher(const VoxelMesher&) = delete;
    VoxelMesher& operator=(const VoxelMesher&) = delete;

    // Movable
    VoxelMesher(VoxelMesher&& other) noexcept;
    VoxelMesher& operator=(VoxelMesher&& other) noexcept;

    /**
     * @brief Initialize the mesher
     *
     * Must be called before any meshing operations.
     */
    void initialize();

    /**
     * @brief Shutdown the mesher
     */
    void shutdown();

    /**
     * @brief Set the output buffer for meshing
     *
     * Must be called before beginChunk(). The buffer should come from
     * a MeshBufferPool to avoid runtime allocation.
     *
     * @param buffer Pointer to buffer data (at least STB_BUFFER_SIZE bytes)
     * @param bufferSize Size of the buffer
     */
    void setBuffer(uint8_t* buffer, size_t bufferSize);

    /**
     * @brief Set buffer from a MeshBuffer (convenience overload)
     */
    void setBuffer(MeshBuffer* meshBuffer);

    /**
     * @brief Begin meshing a new chunk
     *
     * @param chunk The chunk voxel data to mesh
     * @param neighbors Array of 6 neighbor chunk pointers (+X, -X, +Y, -Y, +Z, -Z)
     *                  Can be nullptr for chunks at world edges
     * @param chunkCoord World chunk coordinate
     */
    void beginChunk(const ChunkVoxelData* chunk,
                   const ChunkVoxelData* neighbors[6],
                   const ChunkCoord& chunkCoord);

    /**
     * @brief Generate mesh vertices
     *
     * May need to be called multiple times if buffer fills.
     * Call until result.volumeDone is true.
     *
     * @return Mesh result with vertex data and metadata
     */
    MeshResult meshify();

    /**
     * @brief Convert stb vertex to our VoxelVertex format
     *
     * @param stbVertex Input stb_voxel_render vertex
     * @param chunkCoord Chunk coordinate for world position
     * @return Converted VoxelVertex
     */
    static VoxelVertex convertVertex(const StbVoxelVertex& stbVertex,
                                    const ChunkCoord& chunkCoord);

    /**
     * @brief Convert entire mesh from stb format to VoxelVertex array
     *
     * @param stbVertices Input stb vertices
     * @param numVertices Number of vertices
     * @param outVertices Output VoxelVertex array (must have space for numVertices)
     * @param chunkCoord Chunk coordinate
     * @param scale Scale factor from meshify result
     */
    static void convertMesh(const StbVoxelVertex* stbVertices,
                           uint32_t numVertices,
                           VoxelVertex* outVertices,
                           const ChunkCoord& chunkCoord,
                           const glm::vec3& scale);

    /**
     * @brief Get raw stb vertex buffer (for direct upload)
     *
     * Useful if your shader can directly unpack stb format.
     */
    const StbVoxelVertex* getStbVertexBuffer() const {
        return reinterpret_cast<const StbVoxelVertex*>(currentBuffer_);
    }

private:
    /// Opaque stb_voxel_render mesh maker (heap allocated)
    void* meshMaker_ = nullptr;

    /// Current buffer pointer (from pool, not owned)
    uint8_t* currentBuffer_ = nullptr;
    size_t currentBufferSize_ = 0;

    /// Current chunk coordinate (for vertex conversion)
    ChunkCoord currentChunk_;

    /// Initialization state
    bool initialized_ = false;
};

/**
 * @brief Generate a simple procedural terrain chunk
 *
 * Fills chunk with simplex noise-based terrain.
 * Good for testing/demos.
 *
 * @param chunk Chunk to fill
 * @param coord Chunk world coordinate
 * @param seed World seed
 */
void generateProceduralTerrain(ChunkVoxelData* chunk,
                              const ChunkCoord& coord,
                              uint32_t seed);

/**
 * @brief Fill chunk with flat terrain at specified height
 *
 * @param chunk Chunk to fill
 * @param groundHeight Height of ground (in local chunk coords, 0-15)
 * @param groundBlock Block type for ground
 */
void generateFlatTerrain(ChunkVoxelData* chunk,
                        int groundHeight,
                        BlockType groundBlock = BLOCK_GRASS);

} // namespace voxel
} // namespace jupiter
