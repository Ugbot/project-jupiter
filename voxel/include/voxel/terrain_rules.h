#pragma once

/**
 * @file terrain_rules.h
 * @brief Configurable terrain generation rules
 *
 * Replaces hardcoded height-based terrain generation with
 * JSON-configurable rules per biome.
 */

#include "voxel_types.h"
#include "block_registry.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>

namespace jupiter {
namespace voxel {

// ============================================================================
// Terrain Layer
// ============================================================================

/**
 * @brief A single layer in terrain generation
 *
 * Defines what block type appears between certain heights,
 * optionally with noise-based variation.
 */
struct TerrainLayer {
    /// Minimum height for this layer (inclusive)
    int32_t minHeight = 0;
    
    /// Maximum height for this layer (inclusive)
    int32_t maxHeight = 0;
    
    /// Block type for this layer
    BlockType blockType = BLOCK_STONE;
    
    /// Block type identifier (resolved at load time)
    std::string blockName;
    
    /// Noise threshold for ore/variation (0.0 = always, 1.0 = never)
    float noiseThreshold = 0.0f;
    
    /// Alternative block when noise threshold not met
    BlockType alternateBlock = BLOCK_STONE;
    
    /// Whether this layer is relative to surface height
    bool relativeToSurface = false;
    
    /**
     * @brief Check if a height falls within this layer
     *
     * @param y World Y coordinate
     * @param surfaceHeight Surface height at this XZ
     * @return true if this layer applies
     */
    bool containsHeight(int32_t y, int32_t surfaceHeight) const {
        int32_t effectiveMin = relativeToSurface ? (surfaceHeight + minHeight) : minHeight;
        int32_t effectiveMax = relativeToSurface ? (surfaceHeight + maxHeight) : maxHeight;
        return y >= effectiveMin && y <= effectiveMax;
    }
    
    /**
     * @brief Get the block type at this position
     *
     * @param noiseValue 3D noise value at this position (0.0-1.0)
     * @return Block type
     */
    BlockType getBlock(float noiseValue) const {
        if (noiseThreshold > 0.0f && noiseValue < noiseThreshold) {
            return alternateBlock;
        }
        return blockType;
    }
};

// ============================================================================
// Ore Definition
// ============================================================================

/**
 * @brief Ore/vein generation parameters
 */
struct OreDefinition {
    std::string name;
    BlockType blockType = BLOCK_STONE;
    
    /// Height range
    int32_t minHeight = 0;
    int32_t maxHeight = 64;
    
    /// Vein size
    uint8_t minVeinSize = 1;
    uint8_t maxVeinSize = 8;
    
    /// Spawn chance per chunk (0.0-1.0)
    float spawnChance = 0.1f;
    
    /// 3D noise threshold (higher = rarer)
    float noiseThreshold = 0.85f;
};

// ============================================================================
// Biome Definition
// ============================================================================

/**
 * @brief Complete biome definition with terrain layers
 */
struct BiomeDefinition {
    /// Biome ID
    uint8_t id = 0;
    
    /// Human-readable name
    std::string name = "default";
    
    /// Identifier for JSON references
    std::string identifier = "default";
    
    /// Base surface height
    float baseHeight = 64.0f;
    
    /// Height variation amplitude
    float heightVariation = 16.0f;
    
    /// Noise frequency for terrain shape
    float noiseFrequency = 0.02f;
    
    /// Terrain layers (bottom to top)
    std::vector<TerrainLayer> layers;
    
    /// Ore definitions for this biome
    std::vector<OreDefinition> ores;
    
    /// Surface block (top of terrain)
    BlockType surfaceBlock = BLOCK_GRASS;
    
    /// Subsurface block (below surface)
    BlockType subsurfaceBlock = BLOCK_DIRT;
    
    /// Subsurface depth
    uint8_t subsurfaceDepth = 3;
    
    /// Fill block (bulk of terrain)
    BlockType fillBlock = BLOCK_STONE;
    
    /// Bottom block (y=0)
    BlockType bottomBlock = BLOCK_STONE;
    
    // ========================================================================
    // Lookup Methods
    // ========================================================================
    
    /**
     * @brief Get block type at a specific height
     *
     * @param y World Y coordinate
     * @param surfaceHeight Calculated surface height at this XZ
     * @param noise3D 3D noise value for ore distribution
     * @return Block type
     */
    BlockType getBlockAt(int32_t y, int32_t surfaceHeight, float noise3D) const {
        // Above surface = air
        if (y > surfaceHeight) {
            return BLOCK_AIR;
        }
        
        // Check layers first (allows custom rules)
        for (const auto& layer : layers) {
            if (layer.containsHeight(y, surfaceHeight)) {
                return layer.getBlock(noise3D);
            }
        }
        
        // Default layer logic
        if (y == 0) {
            return bottomBlock;
        }
        if (y == surfaceHeight) {
            return surfaceBlock;
        }
        if (y > surfaceHeight - subsurfaceDepth) {
            return subsurfaceBlock;
        }
        
        // Check ores
        for (const auto& ore : ores) {
            if (y >= ore.minHeight && y <= ore.maxHeight) {
                if (noise3D > ore.noiseThreshold) {
                    return ore.blockType;
                }
            }
        }
        
        return fillBlock;
    }
    
    /**
     * @brief Calculate surface height at a position
     *
     * @param noiseValue 2D noise value (0.0-1.0)
     * @return Surface Y coordinate
     */
    int32_t getSurfaceHeight(float noiseValue) const {
        return static_cast<int32_t>(baseHeight + noiseValue * heightVariation);
    }
    
    /**
     * @brief Get SDF density at a position
     *
     * @param worldY World Y coordinate
     * @param surfaceHeight Surface height at this XZ
     * @return Density (negative = inside terrain, positive = outside)
     */
    float getDensity(float worldY, float surfaceHeight) const {
        return worldY - surfaceHeight;
    }
};

// ============================================================================
// Terrain Rules
// ============================================================================

/**
 * @brief Manager for terrain generation rules
 *
 * Loads biomes and terrain rules from JSON configuration.
 * Provides lookup methods for block types and density values.
 */
class TerrainRules {
public:
    /// Maximum number of biomes
    static constexpr size_t MAX_BIOMES = 256;
    
    /**
     * @brief Get singleton instance
     */
    static TerrainRules& instance() {
        static TerrainRules rules;
        return rules;
    }
    
    // ========================================================================
    // Loading
    // ========================================================================
    
    /**
     * @brief Load terrain rules from JSON file
     */
    bool loadFromJSON(const std::string& path);
    
    /**
     * @brief Load terrain rules from JSON string
     */
    bool loadFromString(const std::string& json);
    
    /**
     * @brief Register a biome definition
     */
    bool registerBiome(const BiomeDefinition& biome);
    
    /**
     * @brief Register default biomes
     */
    void registerDefaults();
    
    // ========================================================================
    // Biome Lookup
    // ========================================================================
    
    /**
     * @brief Get biome by ID
     */
    const BiomeDefinition& getBiome(uint8_t id) const {
        return biomes_[id];
    }
    
    /**
     * @brief Get biome by name
     */
    const BiomeDefinition* getBiomeByName(const std::string& name) const {
        auto it = nameToId_.find(name);
        if (it == nameToId_.end()) return nullptr;
        return &biomes_[it->second];
    }
    
    /**
     * @brief Get biome ID at world position
     *
     * Uses 2D noise to determine biome boundaries.
     *
     * @param worldX World X coordinate
     * @param worldZ World Z coordinate
     * @param seed World seed
     * @return Biome ID
     */
    uint8_t getBiomeAt(float worldX, float worldZ, uint32_t seed) const;
    
    // ========================================================================
    // Block Lookup
    // ========================================================================
    
    /**
     * @brief Get block type at world position
     *
     * @param biomeId Biome ID at this position
     * @param y World Y coordinate
     * @param surfaceHeight Surface height at this XZ
     * @param noise3D 3D noise value for ore distribution
     * @return Block type
     */
    BlockType getBlockAt(uint8_t biomeId, int32_t y, int32_t surfaceHeight,
                         float noise3D) const {
        return biomes_[biomeId].getBlockAt(y, surfaceHeight, noise3D);
    }
    
    /**
     * @brief Get surface height at position
     *
     * @param biomeId Biome ID
     * @param noiseValue 2D noise value
     * @return Surface Y coordinate
     */
    int32_t getSurfaceHeight(uint8_t biomeId, float noiseValue) const {
        return biomes_[biomeId].getSurfaceHeight(noiseValue);
    }
    
    /**
     * @brief Get SDF density at position
     *
     * @param biomeId Biome ID
     * @param worldY World Y coordinate
     * @param surfaceHeight Surface height at this XZ
     * @return Density value
     */
    float getDensity(uint8_t biomeId, float worldY, float surfaceHeight) const {
        return biomes_[biomeId].getDensity(worldY, surfaceHeight);
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    size_t getBiomeCount() const { return nameToId_.size(); }
    
private:
    TerrainRules() {
        for (auto& biome : biomes_) {
            biome = BiomeDefinition{};
        }
    }
    
    ~TerrainRules() = default;
    
    TerrainRules(const TerrainRules&) = delete;
    TerrainRules& operator=(const TerrainRules&) = delete;
    
    std::array<BiomeDefinition, MAX_BIOMES> biomes_;
    std::unordered_map<std::string, uint8_t> nameToId_;
};

} // namespace voxel
} // namespace jupiter



