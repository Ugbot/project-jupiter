/**
 * @file terrain_rules.cpp
 * @brief Implementation of TerrainRules
 */

#include <voxel/terrain_rules.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

namespace jupiter {
namespace voxel {

// ============================================================================
// Simple JSON Parser Helpers (same as block_registry.cpp)
// ============================================================================

namespace {

const char* skipWS(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}

const char* parseString(const char* p, std::string& out) {
    p = skipWS(p);
    if (*p != '"') return nullptr;
    ++p;
    out.clear();
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) {
            ++p;
            switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                default: out += *p;
            }
        } else {
            out += *p;
        }
        ++p;
    }
    if (*p == '"') ++p;
    return p;
}

const char* parseNumber(const char* p, double& out) {
    p = skipWS(p);
    char* end = nullptr;
    out = std::strtod(p, &end);
    return end;
}

const char* parseBool(const char* p, bool& out) {
    p = skipWS(p);
    if (strncmp(p, "true", 4) == 0) { out = true; return p + 4; }
    if (strncmp(p, "false", 5) == 0) { out = false; return p + 5; }
    return nullptr;
}

const char* findKey(const char* p, const char* key) {
    while (*p) {
        p = skipWS(p);
        if (*p == '}') return nullptr;
        
        std::string currentKey;
        const char* next = parseString(p, currentKey);
        if (!next) return nullptr;
        
        p = skipWS(next);
        if (*p != ':') return nullptr;
        ++p;
        
        if (currentKey == key) {
            return skipWS(p);
        }
        
        p = skipWS(p);
        int depth = 0;
        bool inString = false;
        while (*p) {
            if (!inString) {
                if (*p == '"') inString = true;
                else if (*p == '{' || *p == '[') ++depth;
                else if (*p == '}' || *p == ']') {
                    if (depth == 0) break;
                    --depth;
                }
                else if (*p == ',' && depth == 0) break;
            } else {
                if (*p == '"' && *(p-1) != '\\') inString = false;
            }
            ++p;
        }
        
        if (*p == ',') ++p;
    }
    return nullptr;
}

// Simple hash for biome noise
uint32_t hash2D(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 374761393u;
    h ^= static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h;
}

} // anonymous namespace

// ============================================================================
// TerrainRules Implementation
// ============================================================================

bool TerrainRules::loadFromJSON(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return loadFromString(buffer.str());
}

bool TerrainRules::loadFromString(const std::string& json) {
    const char* p = json.c_str();
    
    p = skipWS(p);
    if (*p != '{') return false;
    ++p;
    
    // Find "biomes" array
    const char* biomes = findKey(p, "biomes");
    if (!biomes) return false;
    
    biomes = skipWS(biomes);
    if (*biomes != '[') return false;
    ++biomes;
    
    uint8_t nextId = 0;
    
    while (*biomes) {
        biomes = skipWS(biomes);
        if (*biomes == ']') break;
        
        if (*biomes == '{') {
            BiomeDefinition biome;
            biome.id = nextId++;
            
            // Parse biome fields
            const char* val = findKey(biomes, "name");
            if (val) parseString(val, biome.name);
            
            val = findKey(biomes, "identifier");
            if (val) parseString(val, biome.identifier);
            else biome.identifier = biome.name;
            
            val = findKey(biomes, "baseHeight");
            if (val) { double d; parseNumber(val, d); biome.baseHeight = static_cast<float>(d); }
            
            val = findKey(biomes, "heightVariation");
            if (val) { double d; parseNumber(val, d); biome.heightVariation = static_cast<float>(d); }
            
            val = findKey(biomes, "noiseFrequency");
            if (val) { double d; parseNumber(val, d); biome.noiseFrequency = static_cast<float>(d); }
            
            val = findKey(biomes, "surfaceBlock");
            if (val) {
                std::string blockName;
                parseString(val, blockName);
                biome.surfaceBlock = BlockRegistry::instance().getByName(blockName);
            }
            
            val = findKey(biomes, "subsurfaceBlock");
            if (val) {
                std::string blockName;
                parseString(val, blockName);
                biome.subsurfaceBlock = BlockRegistry::instance().getByName(blockName);
            }
            
            val = findKey(biomes, "fillBlock");
            if (val) {
                std::string blockName;
                parseString(val, blockName);
                biome.fillBlock = BlockRegistry::instance().getByName(blockName);
            }
            
            val = findKey(biomes, "subsurfaceDepth");
            if (val) { double d; parseNumber(val, d); biome.subsurfaceDepth = static_cast<uint8_t>(d); }
            
            // Parse layers
            val = findKey(biomes, "layers");
            if (val && *val == '[') {
                const char* layerP = val + 1;
                while (*layerP) {
                    layerP = skipWS(layerP);
                    if (*layerP == ']') break;
                    
                    if (*layerP == '{') {
                        TerrainLayer layer;
                        
                        const char* lv = findKey(layerP, "minHeight");
                        if (lv) { double d; parseNumber(lv, d); layer.minHeight = static_cast<int32_t>(d); }
                        
                        lv = findKey(layerP, "maxHeight");
                        if (lv) { double d; parseNumber(lv, d); layer.maxHeight = static_cast<int32_t>(d); }
                        
                        lv = findKey(layerP, "block");
                        if (lv) {
                            parseString(lv, layer.blockName);
                            layer.blockType = BlockRegistry::instance().getByName(layer.blockName);
                        }
                        
                        lv = findKey(layerP, "noiseThreshold");
                        if (lv) { double d; parseNumber(lv, d); layer.noiseThreshold = static_cast<float>(d); }
                        
                        lv = findKey(layerP, "relativeToSurface");
                        if (lv) parseBool(lv, layer.relativeToSurface);
                        
                        biome.layers.push_back(layer);
                        
                        // Skip layer object
                        int depth = 1;
                        ++layerP;
                        while (*layerP && depth > 0) {
                            if (*layerP == '{') ++depth;
                            else if (*layerP == '}') --depth;
                            ++layerP;
                        }
                    }
                    
                    layerP = skipWS(layerP);
                    if (*layerP == ',') ++layerP;
                }
            }
            
            registerBiome(biome);
            
            // Skip biome object
            int depth = 1;
            ++biomes;
            while (*biomes && depth > 0) {
                if (*biomes == '{') ++depth;
                else if (*biomes == '}') --depth;
                ++biomes;
            }
        }
        
        biomes = skipWS(biomes);
        if (*biomes == ',') ++biomes;
    }
    
    return true;
}

bool TerrainRules::registerBiome(const BiomeDefinition& biome) {
    if (biome.id >= MAX_BIOMES) {
        return false;
    }
    
    biomes_[biome.id] = biome;
    
    if (!biome.identifier.empty()) {
        nameToId_[biome.identifier] = biome.id;
    }
    if (!biome.name.empty() && biome.name != biome.identifier) {
        nameToId_[biome.name] = biome.id;
    }
    
    return true;
}

void TerrainRules::registerDefaults() {
    // Plains biome (ID 0)
    {
        BiomeDefinition plains;
        plains.id = 0;
        plains.name = "Plains";
        plains.identifier = "plains";
        plains.baseHeight = 64.0f;
        plains.heightVariation = 8.0f;
        plains.noiseFrequency = 0.01f;
        plains.surfaceBlock = BLOCK_GRASS;
        plains.subsurfaceBlock = BLOCK_DIRT;
        plains.subsurfaceDepth = 3;
        plains.fillBlock = BLOCK_STONE;
        plains.bottomBlock = BLOCK_STONE;
        registerBiome(plains);
    }
    
    // Mountains biome (ID 1)
    {
        BiomeDefinition mountains;
        mountains.id = 1;
        mountains.name = "Mountains";
        mountains.identifier = "mountains";
        mountains.baseHeight = 80.0f;
        mountains.heightVariation = 40.0f;
        mountains.noiseFrequency = 0.015f;
        mountains.surfaceBlock = BLOCK_STONE;
        mountains.subsurfaceBlock = BLOCK_STONE;
        mountains.subsurfaceDepth = 0;
        mountains.fillBlock = BLOCK_STONE;
        mountains.bottomBlock = BLOCK_STONE;
        registerBiome(mountains);
    }
    
    // Desert biome (ID 2)
    {
        BiomeDefinition desert;
        desert.id = 2;
        desert.name = "Desert";
        desert.identifier = "desert";
        desert.baseHeight = 64.0f;
        desert.heightVariation = 4.0f;
        desert.noiseFrequency = 0.008f;
        desert.surfaceBlock = BLOCK_SAND;
        desert.subsurfaceBlock = BLOCK_SAND;
        desert.subsurfaceDepth = 5;
        desert.fillBlock = BLOCK_STONE;
        desert.bottomBlock = BLOCK_STONE;
        registerBiome(desert);
    }
    
    // Forest biome (ID 3)
    {
        BiomeDefinition forest;
        forest.id = 3;
        forest.name = "Forest";
        forest.identifier = "forest";
        forest.baseHeight = 68.0f;
        forest.heightVariation = 12.0f;
        forest.noiseFrequency = 0.012f;
        forest.surfaceBlock = BLOCK_GRASS;
        forest.subsurfaceBlock = BLOCK_DIRT;
        forest.subsurfaceDepth = 4;
        forest.fillBlock = BLOCK_STONE;
        forest.bottomBlock = BLOCK_STONE;
        registerBiome(forest);
    }
}

uint8_t TerrainRules::getBiomeAt(float worldX, float worldZ, uint32_t seed) const {
    // Simple biome selection based on 2D noise
    // For production, use a more sophisticated method (Voronoi, temperature/humidity)
    
    const float biomeScale = 0.002f;  // Large-scale biome regions
    
    int32_t bx = static_cast<int32_t>(worldX * biomeScale);
    int32_t bz = static_cast<int32_t>(worldZ * biomeScale);
    
    uint32_t h = hash2D(bx, bz, seed);
    
    // Map hash to biome count
    size_t biomeCount = nameToId_.size();
    if (biomeCount == 0) return 0;
    
    return static_cast<uint8_t>(h % biomeCount);
}

} // namespace voxel
} // namespace jupiter



