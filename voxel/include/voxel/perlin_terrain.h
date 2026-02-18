#pragma once

/**
 * @file perlin_terrain.h
 * @brief Perlin noise-based terrain and cave generation
 *
 * Uses GLM's Perlin noise for:
 * - 2D noise for surface height variation
 * - 3D noise isosurface intersection for cave systems
 *
 * Cave generation based on: https://blog.danol.cz/voxel-cave-generation-using-3d-perlin-noise-isosurfaces/
 */

#include "voxel_types.h"
#include "voxel_column.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace jupiter {
namespace voxel {

/**
 * @brief Configuration for terrain generation
 */
struct PerlinTerrainConfig {
    // ========================================================================
    // Surface Generation
    // ========================================================================
    
    /// Base surface height (sea level)
    float baseHeight = 64.0f;
    
    /// Surface height amplitude
    float heightAmplitude = 32.0f;
    
    /// Primary surface noise frequency (lower = larger features)
    float surfaceFrequency = 0.01f;
    
    /// Secondary detail noise frequency
    float detailFrequency = 0.05f;
    
    /// Detail noise amplitude ratio
    float detailAmplitude = 0.2f;
    
    // ========================================================================
    // Cave Generation (Isosurface Intersection)
    // ========================================================================
    
    /// Enable cave generation
    bool enableCaves = true;
    
    /// Cave noise frequency (controls tunnel size)
    float caveFrequency = 0.02f;
    
    /// Cave isosurface threshold (smaller = thinner tunnels)
    float caveThreshold = 0.08f;
    
    /// Second cave noise offset for intersection
    glm::vec3 caveOffset = glm::vec3(1000.0f, 0.0f, 1000.0f);
    
    /// Minimum Y for caves (to prevent surface caves)
    float caveMinY = 10.0f;
    
    /// Maximum Y for caves (caves taper off near surface)
    float caveMaxY = 55.0f;
    
    // ========================================================================
    // Material Assignment
    // ========================================================================
    
    /// Block type for stone (deep underground)
    BlockType stoneBlock = BLOCK_STONE;
    
    /// Block type for dirt (near surface)
    BlockType dirtBlock = BLOCK_DIRT;
    
    /// Block type for grass (surface)
    BlockType grassBlock = BLOCK_GRASS;
    
    /// Dirt layer thickness below surface
    int dirtDepth = 4;
    
    // ========================================================================
    // Seeds
    // ========================================================================
    
    /// World seed
    uint32_t seed = 12345;
    
    /// Seed offset for cave noise A
    uint32_t caveSeedA = 3543;
    
    /// Seed offset for cave noise B
    uint32_t caveSeedB = 43264;
};

/**
 * @brief Perlin noise-based terrain generator
 *
 * Generates terrain using:
 * - 2D Perlin noise for surface height
 * - Octave noise for detail
 * - 3D Perlin noise isosurface intersection for caves
 *
 * The density field is computed as:
 *   terrain_density = y - surface_height
 *   cave_density = isosurface_intersection ? positive : negative
 *   final_density = combine(terrain_density, cave_density)
 */
class PerlinTerrainGenerator {
public:
    PerlinTerrainGenerator() = default;
    explicit PerlinTerrainGenerator(const PerlinTerrainConfig& config);
    
    /**
     * @brief Set configuration
     */
    void setConfig(const PerlinTerrainConfig& config) {
        config_ = config;
    }
    
    /**
     * @brief Get current configuration
     */
    const PerlinTerrainConfig& getConfig() const {
        return config_;
    }
    
    // ========================================================================
    // Height Queries
    // ========================================================================
    
    /**
     * @brief Get surface height at world XZ position
     *
     * Uses multi-octave 2D Perlin noise.
     *
     * @param worldX World X coordinate
     * @param worldZ World Z coordinate
     * @return Surface height (Y coordinate)
     */
    float getSurfaceHeight(float worldX, float worldZ) const;
    
    /**
     * @brief Get surface height with octave detail
     */
    float getSurfaceHeightOctaves(float worldX, float worldZ, int octaves) const;
    
    // ========================================================================
    // Cave Queries
    // ========================================================================
    
    /**
     * @brief Check if position is inside a cave
     *
     * Uses isosurface intersection of two 3D Perlin noise fields.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return true if position is inside a cave
     */
    bool isCave(float worldX, float worldY, float worldZ) const;
    
    /**
     * @brief Get cave density at position
     *
     * Returns a smooth density value for caves.
     * Negative = inside cave, Positive = solid
     *
     * @return Cave density value
     */
    float getCaveDensity(float worldX, float worldY, float worldZ) const;
    
    // ========================================================================
    // Density Field (for Marching Cubes)
    // ========================================================================
    
    /**
     * @brief Get combined terrain density at position
     *
     * Combines surface height and cave density into a single SDF.
     * Negative = inside solid, Positive = empty/air
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Density value for smooth meshing
     */
    float getDensity(float worldX, float worldY, float worldZ) const;
    
    // ========================================================================
    // Material Assignment
    // ========================================================================
    
    /**
     * @brief Get block type at position
     *
     * Assigns material based on depth from surface.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Block type
     */
    BlockType getBlockType(float worldX, float worldY, float worldZ) const;
    
    // ========================================================================
    // Chunk Generation
    // ========================================================================
    
    /**
     * @brief Generate density and material data for a chunk
     *
     * Fills the chunk's density and block arrays.
     *
     * @param chunk Output chunk columns
     * @param chunkCoord Chunk coordinate
     */
    void generateChunk(ChunkColumns& chunk, const ChunkCoord& chunkCoord) const;
    
    /**
     * @brief Generate only density field (for smooth meshing)
     */
    void generateDensity(ChunkColumns& chunk, const ChunkCoord& chunkCoord) const;
    
    /**
     * @brief Generate only block types (for blocky meshing)
     */
    void generateBlocks(ChunkColumns& chunk, const ChunkCoord& chunkCoord) const;
    
private:
    /**
     * @brief Seed-offset Perlin noise
     */
    float perlin3D(const glm::vec3& pos, uint32_t seedOffset) const;
    float perlin2D(const glm::vec2& pos, uint32_t seedOffset) const;
    
    /**
     * @brief Get cave fade factor based on Y position
     *
     * Caves fade out near surface and bottom.
     */
    float getCaveFade(float worldY) const;
    
    PerlinTerrainConfig config_;
};

} // namespace voxel
} // namespace jupiter



