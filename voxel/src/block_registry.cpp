/**
 * @file block_registry.cpp
 * @brief Implementation of BlockRegistry
 */

#include <voxel/block_registry.h>
#include <fstream>
#include <sstream>
#include <cstring>

// Simple JSON parsing (no external dependency)
// For production, consider using nlohmann/json or rapidjson

namespace jupiter {
namespace voxel {

// ============================================================================
// Simple JSON Parser Helpers
// ============================================================================

namespace {

// Skip whitespace
const char* skipWS(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}

// Parse a quoted string
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

// Parse a number (int or float)
const char* parseNumber(const char* p, double& out) {
    p = skipWS(p);
    char* end = nullptr;
    out = std::strtod(p, &end);
    return end;
}

// Parse a boolean
const char* parseBool(const char* p, bool& out) {
    p = skipWS(p);
    if (strncmp(p, "true", 4) == 0) {
        out = true;
        return p + 4;
    }
    if (strncmp(p, "false", 5) == 0) {
        out = false;
        return p + 5;
    }
    return nullptr;
}

// Find a key in current object
const char* findKey(const char* p, const char* key) {
    size_t keyLen = strlen(key);
    while (*p) {
        p = skipWS(p);
        if (*p == '}') return nullptr;  // End of object
        
        std::string currentKey;
        const char* next = parseString(p, currentKey);
        if (!next) return nullptr;
        
        p = skipWS(next);
        if (*p != ':') return nullptr;
        ++p;
        
        if (currentKey == key) {
            return skipWS(p);
        }
        
        // Skip this value
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

// Parse a block definition from JSON object
bool parseBlockDef(const char* p, BlockDefinition& def) {
    p = skipWS(p);
    if (*p != '{') return false;
    ++p;
    
    // Parse id
    const char* val = findKey(p, "id");
    if (val) {
        double d;
        parseNumber(val, d);
        def.id = static_cast<uint8_t>(d);
    }
    
    // Parse name
    val = findKey(p, "name");
    if (val) parseString(val, def.name);
    
    // Parse identifier
    val = findKey(p, "identifier");
    if (val) parseString(val, def.identifier);
    else def.identifier = def.name;
    
    // Parse textures
    val = findKey(p, "textureTop");
    if (val) { double d; parseNumber(val, d); def.render.textureTop = static_cast<uint8_t>(d); }
    
    val = findKey(p, "textureSide");
    if (val) { double d; parseNumber(val, d); def.render.textureSide = static_cast<uint8_t>(d); }
    
    val = findKey(p, "textureBottom");
    if (val) { double d; parseNumber(val, d); def.render.textureBottom = static_cast<uint8_t>(d); }
    
    // Parse texture (single value for all faces)
    val = findKey(p, "texture");
    if (val) {
        double d;
        parseNumber(val, d);
        uint8_t tex = static_cast<uint8_t>(d);
        def.render.textureTop = tex;
        def.render.textureSide = tex;
        def.render.textureBottom = tex;
    }
    
    // Parse properties
    val = findKey(p, "isSolid");
    if (val) parseBool(val, def.physics.isSolid);
    
    val = findKey(p, "isTransparent");
    if (val) parseBool(val, def.render.isTransparent);
    
    val = findKey(p, "density");
    if (val) { double d; parseNumber(val, d); def.density = static_cast<float>(d); }
    
    val = findKey(p, "hardness");
    if (val) { double d; parseNumber(val, d); def.hardness = static_cast<float>(d); }
    
    val = findKey(p, "emissive");
    if (val) { double d; parseNumber(val, d); def.render.emissive = static_cast<uint8_t>(d); }
    
    // Parse material type
    val = findKey(p, "material");
    if (val) {
        std::string matStr;
        parseString(val, matStr);
        if (matStr == "solid") def.material = BlockMaterial::Solid;
        else if (matStr == "liquid") def.material = BlockMaterial::Liquid;
        else if (matStr == "gas") def.material = BlockMaterial::Gas;
        else if (matStr == "vegetation") def.material = BlockMaterial::Vegetation;
    }
    
    return true;
}

} // anonymous namespace

// ============================================================================
// BlockRegistry Implementation
// ============================================================================

bool BlockRegistry::loadFromJSON(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return loadFromString(buffer.str());
}

bool BlockRegistry::loadFromString(const std::string& json) {
    const char* p = json.c_str();
    
    p = skipWS(p);
    if (*p != '{') return false;
    ++p;
    
    // Find "blocks" array
    const char* blocks = findKey(p, "blocks");
    if (!blocks) return false;
    
    blocks = skipWS(blocks);
    if (*blocks != '[') return false;
    ++blocks;
    
    // Parse each block
    while (*blocks) {
        blocks = skipWS(blocks);
        if (*blocks == ']') break;
        
        if (*blocks == '{') {
            BlockDefinition def;
            if (parseBlockDef(blocks, def)) {
                registerBlock(def);
            }
            
            // Skip to next block
            int depth = 1;
            ++blocks;
            while (*blocks && depth > 0) {
                if (*blocks == '{') ++depth;
                else if (*blocks == '}') --depth;
                ++blocks;
            }
        }
        
        blocks = skipWS(blocks);
        if (*blocks == ',') ++blocks;
    }
    
    return true;
}

bool BlockRegistry::registerBlock(const BlockDefinition& def) {
    if (def.id >= MAX_BLOCKS) {
        return false;
    }
    
    blocks_[def.id] = def;
    
    if (!def.identifier.empty()) {
        nameToId_[def.identifier] = def.id;
    }
    if (!def.name.empty() && def.name != def.identifier) {
        nameToId_[def.name] = def.id;
    }
    
    return true;
}

void BlockRegistry::registerDefaults() {
    // Air (ID 0)
    {
        BlockDefinition air;
        air.id = BLOCK_AIR;
        air.name = "Air";
        air.identifier = "air";
        air.material = BlockMaterial::Gas;
        air.physics.isSolid = false;
        air.render.isTransparent = true;
        air.render.isCulled = false;
        air.density = -1.0f;
        registerBlock(air);
    }
    
    // Stone (ID 1)
    {
        BlockDefinition stone;
        stone.id = BLOCK_STONE;
        stone.name = "Stone";
        stone.identifier = "stone";
        stone.material = BlockMaterial::Solid;
        stone.render.textureTop = 1;
        stone.render.textureSide = 1;
        stone.render.textureBottom = 1;
        stone.hardness = 1.5f;
        stone.density = 1.0f;
        registerBlock(stone);
    }
    
    // Dirt (ID 2)
    {
        BlockDefinition dirt;
        dirt.id = BLOCK_DIRT;
        dirt.name = "Dirt";
        dirt.identifier = "dirt";
        dirt.material = BlockMaterial::Solid;
        dirt.render.textureTop = 2;
        dirt.render.textureSide = 2;
        dirt.render.textureBottom = 2;
        dirt.hardness = 0.5f;
        dirt.density = 1.0f;
        registerBlock(dirt);
    }
    
    // Grass (ID 3)
    {
        BlockDefinition grass;
        grass.id = BLOCK_GRASS;
        grass.name = "Grass";
        grass.identifier = "grass";
        grass.material = BlockMaterial::Solid;
        grass.render.textureTop = 3;  // Grass top
        grass.render.textureSide = 4; // Grass side
        grass.render.textureBottom = 2; // Dirt bottom
        grass.hardness = 0.6f;
        grass.density = 1.0f;
        registerBlock(grass);
    }
    
    // Sand (ID 4)
    {
        BlockDefinition sand;
        sand.id = BLOCK_SAND;
        sand.name = "Sand";
        sand.identifier = "sand";
        sand.material = BlockMaterial::Solid;
        sand.render.textureTop = 5;
        sand.render.textureSide = 5;
        sand.render.textureBottom = 5;
        sand.hardness = 0.5f;
        sand.density = 1.0f;
        registerBlock(sand);
    }
    
    // Water (ID 5)
    {
        BlockDefinition water;
        water.id = BLOCK_WATER;
        water.name = "Water";
        water.identifier = "water";
        water.material = BlockMaterial::Liquid;
        water.physics.isSolid = false;
        water.render.isTransparent = true;
        water.render.textureTop = 6;
        water.render.textureSide = 6;
        water.render.textureBottom = 6;
        water.density = 0.0f;  // Surface
        registerBlock(water);
    }
    
    // Wood (ID 6)
    {
        BlockDefinition wood;
        wood.id = BLOCK_WOOD;
        wood.name = "Wood";
        wood.identifier = "wood";
        wood.material = BlockMaterial::Solid;
        wood.render.textureTop = 7;  // Log top
        wood.render.textureSide = 8; // Log side
        wood.render.textureBottom = 7;
        wood.hardness = 2.0f;
        wood.density = 1.0f;
        registerBlock(wood);
    }
    
    // Leaves (ID 7)
    {
        BlockDefinition leaves;
        leaves.id = BLOCK_LEAVES;
        leaves.name = "Leaves";
        leaves.identifier = "leaves";
        leaves.material = BlockMaterial::Vegetation;
        leaves.render.isTransparent = true;
        leaves.render.textureTop = 9;
        leaves.render.textureSide = 9;
        leaves.render.textureBottom = 9;
        leaves.hardness = 0.2f;
        leaves.density = 0.5f;
        registerBlock(leaves);
    }
}

} // namespace voxel
} // namespace jupiter



