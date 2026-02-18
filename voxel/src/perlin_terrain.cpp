/**
 * @file perlin_terrain.cpp
 * @brief Implementation of fast hash noise terrain generator
 *
 * Uses fast hash-based noise instead of expensive glm::perlin for performance.
 * See: https://www.shadertoy.com/view/4dS3Wd for similar techniques.
 */

#include <voxel/perlin_terrain.h>
#include <cmath>
#include <algorithm>

namespace jupiter {
namespace voxel {

// ============================================================================
// Fast Hash Noise Implementation
// ============================================================================

namespace {

// Fast hash function (FNV-1a style)
inline uint32_t hash1(int32_t x, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h;
}

inline uint32_t hash2(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h;
}

inline uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(y) * 668265263u;
    h ^= static_cast<uint32_t>(z) * 1274126177u;
    h = (h ^ (h >> 13)) * 2654435769u;
    return h;
}

// Convert hash to float in range [-1, 1]
inline float hashToFloat(uint32_t h) {
    return static_cast<float>(h & 0xFFFFu) / 32768.0f - 1.0f;
}

// Smooth interpolation (Hermite curve)
inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Linear interpolation
inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// 2D value noise with smooth interpolation
float valueNoise2D(float x, float z, uint32_t seed) {
    // Integer cell coordinates
    int ix = static_cast<int>(std::floor(x));
    int iz = static_cast<int>(std::floor(z));
    
    // Fractional part within cell
    float fx = x - static_cast<float>(ix);
    float fz = z - static_cast<float>(iz);
    
    // Smoothstep the fractional parts
    float sx = smoothstep(fx);
    float sz = smoothstep(fz);
    
    // Hash the four corners
    float n00 = hashToFloat(hash2(ix, iz, seed));
    float n10 = hashToFloat(hash2(ix + 1, iz, seed));
    float n01 = hashToFloat(hash2(ix, iz + 1, seed));
    float n11 = hashToFloat(hash2(ix + 1, iz + 1, seed));
    
    // Bilinear interpolation
    float n0 = lerp(n00, n10, sx);
    float n1 = lerp(n01, n11, sx);
    return lerp(n0, n1, sz);
}

// 3D value noise with smooth interpolation
float valueNoise3D(float x, float y, float z, uint32_t seed) {
    // Integer cell coordinates
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    int iz = static_cast<int>(std::floor(z));
    
    // Fractional part within cell
    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);
    float fz = z - static_cast<float>(iz);
    
    // Smoothstep the fractional parts
    float sx = smoothstep(fx);
    float sy = smoothstep(fy);
    float sz = smoothstep(fz);
    
    // Hash the eight corners
    float n000 = hashToFloat(hash3(ix, iy, iz, seed));
    float n100 = hashToFloat(hash3(ix + 1, iy, iz, seed));
    float n010 = hashToFloat(hash3(ix, iy + 1, iz, seed));
    float n110 = hashToFloat(hash3(ix + 1, iy + 1, iz, seed));
    float n001 = hashToFloat(hash3(ix, iy, iz + 1, seed));
    float n101 = hashToFloat(hash3(ix + 1, iy, iz + 1, seed));
    float n011 = hashToFloat(hash3(ix, iy + 1, iz + 1, seed));
    float n111 = hashToFloat(hash3(ix + 1, iy + 1, iz + 1, seed));
    
    // Trilinear interpolation
    float n00 = lerp(n000, n100, sx);
    float n10 = lerp(n010, n110, sx);
    float n01 = lerp(n001, n101, sx);
    float n11 = lerp(n011, n111, sx);
    
    float n0 = lerp(n00, n10, sy);
    float n1 = lerp(n01, n11, sy);
    
    return lerp(n0, n1, sz);
}

} // anonymous namespace

// ============================================================================
// Constructor
// ============================================================================

PerlinTerrainGenerator::PerlinTerrainGenerator(const PerlinTerrainConfig& config)
    : config_(config)
{
}

// ============================================================================
// Noise Helpers (now using fast hash noise)
// ============================================================================

float PerlinTerrainGenerator::perlin3D(const glm::vec3& pos, uint32_t seedOffset) const {
    // Use fast 3D value noise instead of glm::perlin
    return valueNoise3D(pos.x, pos.y, pos.z, config_.seed ^ seedOffset);
}

float PerlinTerrainGenerator::perlin2D(const glm::vec2& pos, uint32_t seedOffset) const {
    // Use fast 2D value noise instead of glm::perlin
    return valueNoise2D(pos.x, pos.y, config_.seed ^ seedOffset);
}

// ============================================================================
// Surface Height
// ============================================================================

float PerlinTerrainGenerator::getSurfaceHeight(float worldX, float worldZ) const {
    // If amplitude is zero, return flat plane at baseHeight (no noise needed)
    if (config_.heightAmplitude == 0.0f) {
        return config_.baseHeight;
    }
    
    // Single fast noise call - detail can be added via octaves if needed
    float scaledX = worldX * config_.surfaceFrequency;
    float scaledZ = worldZ * config_.surfaceFrequency;
    float height = valueNoise2D(scaledX, scaledZ, config_.seed);
    
    // Scale and offset
    return config_.baseHeight + height * config_.heightAmplitude;
}

float PerlinTerrainGenerator::getSurfaceHeightOctaves(float worldX, float worldZ, int octaves) const {
    float height = 0.0f;
    float amplitude = 1.0f;
    float frequency = config_.surfaceFrequency;
    float totalAmplitude = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        glm::vec2 pos(worldX * frequency, worldZ * frequency);
        height += perlin2D(pos, config_.seed + i * 1000) * amplitude;
        totalAmplitude += amplitude;
        
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    
    // Normalize and scale
    height /= totalAmplitude;
    return config_.baseHeight + height * config_.heightAmplitude;
}

// ============================================================================
// Cave Generation
// ============================================================================

float PerlinTerrainGenerator::getCaveFade(float worldY) const {
    if (!config_.enableCaves) return 0.0f;
    
    // Smooth fade at cave boundaries (local smoothstep to avoid glm dependency)
    auto smoothFade = [](float edge0, float edge1, float x) -> float {
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    
    float fadeBottom = smoothFade(0.0f, config_.caveMinY, worldY);
    float fadeTop = smoothFade(config_.caveMaxY + 10.0f, config_.caveMaxY, worldY);
    
    return fadeBottom * fadeTop;
}

bool PerlinTerrainGenerator::isCave(float worldX, float worldY, float worldZ) const {
    if (!config_.enableCaves) return false;
    
    // Check Y bounds
    if (worldY < config_.caveMinY || worldY > config_.caveMaxY + 10.0f) {
        return false;
    }
    
    // Isosurface intersection technique:
    // A cave exists where both noise fields are near zero
    glm::vec3 pos(worldX, worldY, worldZ);
    glm::vec3 scaledPos = pos * config_.caveFrequency;
    
    float noiseA = perlin3D(scaledPos, config_.caveSeedA);
    float noiseB = perlin3D(scaledPos + config_.caveOffset * config_.caveFrequency, config_.caveSeedB);
    
    // Both must be within threshold of zero
    return std::abs(noiseA) < config_.caveThreshold &&
           std::abs(noiseB) < config_.caveThreshold;
}

float PerlinTerrainGenerator::getCaveDensity(float worldX, float worldY, float worldZ) const {
    if (!config_.enableCaves) return 1.0f;  // Solid
    
    float fade = getCaveFade(worldY);
    if (fade < 0.001f) return 1.0f;  // Outside cave zone
    
    glm::vec3 pos(worldX, worldY, worldZ);
    glm::vec3 scaledPos = pos * config_.caveFrequency;
    
    float noiseA = perlin3D(scaledPos, config_.caveSeedA);
    float noiseB = perlin3D(scaledPos + config_.caveOffset * config_.caveFrequency, config_.caveSeedB);
    
    // Compute distance from isosurface intersection
    // Using max of the two distances (intersection = both near zero)
    float distA = std::abs(noiseA) - config_.caveThreshold;
    float distB = std::abs(noiseB) - config_.caveThreshold;
    
    // Positive when inside cave (both below threshold)
    float caveDist = -std::max(distA, distB);
    
    // Apply fade
    return caveDist * fade;
}

// ============================================================================
// Combined Density
// ============================================================================

float PerlinTerrainGenerator::getDensity(float worldX, float worldY, float worldZ) const {
    // Get surface height at this XZ
    float surfaceHeight = getSurfaceHeight(worldX, worldZ);
    
    // Basic terrain density (negative = below surface = solid)
    float terrainDensity = worldY - surfaceHeight;
    
    // Get cave density (positive = inside cave = empty)
    float caveDensity = getCaveDensity(worldX, worldY, worldZ);
    
    // Combine: terrain is the base, caves carve into it
    // If we're below surface (terrainDensity < 0) and in a cave (caveDensity > 0),
    // the cave wins and we get empty space
    if (terrainDensity < 0.0f && caveDensity > 0.0f) {
        // Blend between terrain and cave based on cave strength
        return terrainDensity + caveDensity * 2.0f;  // Scale cave effect
    }
    
    return terrainDensity;
}

// ============================================================================
// Material Assignment
// ============================================================================

BlockType PerlinTerrainGenerator::getBlockType(float worldX, float worldY, float worldZ) const {
    float surfaceHeight = getSurfaceHeight(worldX, worldZ);
    float depth = surfaceHeight - worldY;
    
    // Above surface = air
    if (depth < 0.0f) {
        return BLOCK_AIR;
    }
    
    // Check if in cave
    if (isCave(worldX, worldY, worldZ)) {
        return BLOCK_AIR;
    }
    
    // Surface layer
    if (depth < 1.0f) {
        return config_.grassBlock;
    }
    
    // Dirt layer
    if (depth < config_.dirtDepth) {
        return config_.dirtBlock;
    }
    
    // Deep underground = stone
    return config_.stoneBlock;
}

// ============================================================================
// Chunk Generation
// ============================================================================

void PerlinTerrainGenerator::generateChunk(ChunkColumns& chunk, const ChunkCoord& chunkCoord) const {
    // Generate blocks (always needed)
    generateBlocks(chunk, chunkCoord);
    
    // Only generate density if density storage is available (smooth mode)
    if (chunk.hasDensity()) {
        generateDensity(chunk, chunkCoord);
    }
}

void PerlinTerrainGenerator::generateDensity(ChunkColumns& chunk, const ChunkCoord& chunkCoord) const {
    // Skip if no density storage
    if (!chunk.hasDensity()) return;
    
    const float worldOffsetX = static_cast<float>(chunkCoord.x * CHUNK_SIZE);
    const float worldOffsetY = static_cast<float>(chunkCoord.y * CHUNK_SIZE);
    const float worldOffsetZ = static_cast<float>(chunkCoord.z * CHUNK_SIZE);
    
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            float worldX = worldOffsetX + x;
            float worldZ = worldOffsetZ + z;
            
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                float worldY = worldOffsetY + y;
                float density = getDensity(worldX, worldY, worldZ);
                // Use ChunkColumns::setDensity which accesses external density storage
                chunk.setDensity(x, y, z, density);
            }
        }
    }
}

void PerlinTerrainGenerator::generateBlocks(ChunkColumns& chunk, const ChunkCoord& chunkCoord) const {
    const float worldOffsetX = static_cast<float>(chunkCoord.x * CHUNK_SIZE);
    const float worldOffsetY = static_cast<float>(chunkCoord.y * CHUNK_SIZE);
    const float worldOffsetZ = static_cast<float>(chunkCoord.z * CHUNK_SIZE);
    
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            VoxelColumn& col = chunk.at(x, z);
            
            float worldX = worldOffsetX + x;
            float worldZ = worldOffsetZ + z;
            
            // Get surface height for this column
            float surfaceHeight = getSurfaceHeight(worldX, worldZ);
            int surfaceY = static_cast<int>(surfaceHeight);
            
            // Fast path: fill column based on height comparison
            // Below surface is solid, above is air
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                float worldY = worldOffsetY + y;
                int intWorldY = static_cast<int>(worldY);
                
                BlockType block;
                
                // Well above surface = air
                if (intWorldY > surfaceY) {
                    block = BLOCK_AIR;
                }
                // At surface = grass  
                else if (intWorldY == surfaceY) {
                    block = config_.grassBlock;
                }
                // Just below surface = dirt
                else if (intWorldY > surfaceY - config_.dirtDepth) {
                    block = config_.dirtBlock;
                }
                // Deep = stone
                else {
                    block = config_.stoneBlock;
                }
                
                col.setBlock(y, block);
            }
            
            // Only check caves if enabled (expensive - skip by default)
            if (config_.enableCaves) {
                int caveMinYInt = static_cast<int>(config_.caveMinY);
                int caveMaxYInt = static_cast<int>(config_.caveMaxY + 10.0f);
                
                for (int y = std::max(0, caveMinYInt); y < std::min(CHUNK_HEIGHT, caveMaxYInt); ++y) {
                    float worldY = worldOffsetY + y;
                    if (col.getBlock(y) != BLOCK_AIR && isCave(worldX, worldY, worldZ)) {
                        col.setBlock(y, BLOCK_AIR);
                    }
                }
            }
            
            col.updateBounds();
        }
    }
    
    chunk.incrementGeneration();
}

} // namespace voxel
} // namespace jupiter

