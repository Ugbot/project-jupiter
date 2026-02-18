#pragma once

/**
 * @file block_registry.h
 * @brief Runtime-configurable block type registry
 *
 * Replaces hardcoded block types with JSON-loaded definitions.
 * Each block can have custom textures, properties, and density values.
 */

#include "voxel_types.h"
#include <array>
#include <string>
#include <unordered_map>

namespace jupiter {
namespace voxel {

// ============================================================================
// Block Properties
// ============================================================================

/**
 * @brief Block material properties
 */
enum class BlockMaterial : uint8_t {
    Solid,          ///< Standard solid block
    Liquid,         ///< Water, lava, etc.
    Gas,            ///< Air, smoke, etc.
    Vegetation,     ///< Plants, grass, etc.
    Custom          ///< User-defined
};

/**
 * @brief Block rendering properties
 */
struct BlockRenderProps {
    uint8_t textureTop = 0;         ///< Texture index for top face (+Y)
    uint8_t textureSide = 0;        ///< Texture index for side faces
    uint8_t textureBottom = 0;      ///< Texture index for bottom face (-Y)
    uint8_t emissive = 0;           ///< Light emission level (0-15)
    bool isTransparent = false;     ///< Whether light passes through
    bool isCulled = true;           ///< Whether hidden faces are culled
};

/**
 * @brief Block physics properties
 */
struct BlockPhysicsProps {
    bool isSolid = true;            ///< Collision enabled
    bool isClimbable = false;       ///< Can climb (ladders, vines)
    float friction = 0.6f;          ///< Surface friction
    float bounciness = 0.0f;        ///< Bounce coefficient
};

// ============================================================================
// Block Definition
// ============================================================================

/**
 * @brief Complete definition of a block type
 *
 * Loaded from JSON at runtime. All blocks are registered
 * in the BlockRegistry by their uint8_t ID.
 */
struct BlockDefinition {
    /// Block ID (0-255)
    uint8_t id = 0;
    
    /// Human-readable name
    std::string name = "unknown";
    
    /// Internal string identifier (for JSON references)
    std::string identifier = "unknown";
    
    /// Material category
    BlockMaterial material = BlockMaterial::Solid;
    
    /// Rendering properties
    BlockRenderProps render;
    
    /// Physics properties
    BlockPhysicsProps physics;
    
    /// SDF density for smooth terrain (positive = solid, negative = empty)
    float density = 1.0f;
    
    /// Hardness for mining (0 = instant, -1 = unbreakable)
    float hardness = 1.0f;
    
    /// Tool type required (bitmask)
    uint8_t toolType = 0;
    
    // ========================================================================
    // Convenience Methods
    // ========================================================================
    
    /**
     * @brief Check if this is air/empty
     */
    bool isAir() const {
        return material == BlockMaterial::Gas && !physics.isSolid;
    }
    
    /**
     * @brief Check if this is a solid block
     */
    bool isSolid() const {
        return physics.isSolid;
    }
    
    /**
     * @brief Check if this is transparent
     */
    bool isTransparent() const {
        return render.isTransparent;
    }
    
    /**
     * @brief Get texture for a specific face
     */
    uint8_t getTexture(uint8_t face) const {
        switch (face) {
            case 2: return render.textureTop;     // +Y
            case 3: return render.textureBottom;  // -Y
            default: return render.textureSide;   // Sides
        }
    }
};

// ============================================================================
// Block Registry
// ============================================================================

/**
 * @brief Singleton registry of all block types
 *
 * Manages block definitions loaded from JSON. Provides fast
 * lookup by ID and name.
 *
 * Usage:
 *   BlockRegistry::instance().loadFromJSON("blocks.json");
 *   const auto& stone = BlockRegistry::instance().get(BLOCK_STONE);
 */
class BlockRegistry {
public:
    /// Maximum number of block types
    static constexpr size_t MAX_BLOCKS = 256;
    
    /**
     * @brief Get singleton instance
     */
    static BlockRegistry& instance() {
        static BlockRegistry registry;
        return registry;
    }
    
    // ========================================================================
    // Loading
    // ========================================================================
    
    /**
     * @brief Load block definitions from JSON file
     *
     * @param path Path to JSON file
     * @return true if loaded successfully
     */
    bool loadFromJSON(const std::string& path);
    
    /**
     * @brief Load block definitions from JSON string
     *
     * @param json JSON content
     * @return true if parsed successfully
     */
    bool loadFromString(const std::string& json);
    
    /**
     * @brief Register a block definition programmatically
     *
     * @param def Block definition to register
     * @return true if registered successfully
     */
    bool registerBlock(const BlockDefinition& def);
    
    /**
     * @brief Register default blocks (air, stone, dirt, etc.)
     */
    void registerDefaults();
    
    // ========================================================================
    // Lookup
    // ========================================================================
    
    /**
     * @brief Get block definition by ID
     */
    const BlockDefinition& get(BlockType id) const {
        return blocks_[id];
    }
    
    /**
     * @brief Get block definition by ID (mutable)
     */
    BlockDefinition& get(BlockType id) {
        return blocks_[id];
    }
    
    /**
     * @brief Get block ID by name
     *
     * @param name Block identifier string
     * @return Block ID, or 0 (air) if not found
     */
    BlockType getByName(const std::string& name) const {
        auto it = nameToId_.find(name);
        return (it != nameToId_.end()) ? it->second : 0;
    }
    
    /**
     * @brief Check if a block ID is registered
     */
    bool hasBlock(BlockType id) const {
        return !blocks_[id].name.empty() && blocks_[id].name != "unknown";
    }
    
    /**
     * @brief Check if a block name is registered
     */
    bool hasBlock(const std::string& name) const {
        return nameToId_.find(name) != nameToId_.end();
    }
    
    // ========================================================================
    // Properties
    // ========================================================================
    
    /**
     * @brief Check if block is solid (fast path)
     */
    bool isSolid(BlockType id) const {
        return blocks_[id].physics.isSolid;
    }
    
    /**
     * @brief Check if block is transparent (fast path)
     */
    bool isTransparent(BlockType id) const {
        return blocks_[id].render.isTransparent;
    }
    
    /**
     * @brief Get density for SDF meshing (fast path)
     */
    float getDensity(BlockType id) const {
        return blocks_[id].density;
    }
    
    /**
     * @brief Get texture for block face
     */
    uint8_t getTexture(BlockType id, uint8_t face) const {
        return blocks_[id].getTexture(face);
    }
    
    // ========================================================================
    // Iteration
    // ========================================================================
    
    /**
     * @brief Get all registered block IDs
     */
    std::vector<BlockType> getRegisteredBlocks() const {
        std::vector<BlockType> result;
        for (size_t i = 0; i < MAX_BLOCKS; ++i) {
            if (hasBlock(static_cast<BlockType>(i))) {
                result.push_back(static_cast<BlockType>(i));
            }
        }
        return result;
    }
    
    /**
     * @brief Get number of registered blocks
     */
    size_t getRegisteredCount() const {
        return nameToId_.size();
    }
    
private:
    BlockRegistry() {
        // Initialize all blocks as "unknown"
        for (auto& block : blocks_) {
            block = BlockDefinition{};
        }
    }
    
    ~BlockRegistry() = default;
    
    // Non-copyable
    BlockRegistry(const BlockRegistry&) = delete;
    BlockRegistry& operator=(const BlockRegistry&) = delete;
    
    /// Block definitions by ID
    std::array<BlockDefinition, MAX_BLOCKS> blocks_;
    
    /// Name to ID mapping
    std::unordered_map<std::string, BlockType> nameToId_;
};

// ============================================================================
// Convenience Macros
// ============================================================================

#define BLOCK_DEF(id) ::jupiter::voxel::BlockRegistry::instance().get(id)
#define BLOCK_ID(name) ::jupiter::voxel::BlockRegistry::instance().getByName(name)

} // namespace voxel
} // namespace jupiter



