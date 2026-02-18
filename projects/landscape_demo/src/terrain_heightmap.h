#pragma once

#include "rendering/primitives.h"
#include "rendering/texture.h"
#include "rendering/vulkan_mesh.h"
#include "rendering/material_system.h"
#include "rendering/scene_manager.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>

namespace landscape {

// Terrain configuration
struct TerrainConfig {
    float size = 1024.0f;          // meters (square)
    uint32_t segments = 256;       // subdivisions
    float heightScale = 50.0f;     // noise multiplier
    uint32_t textureRes = 512;     // heightmap texture resolution
};

/**
 * @brief Generates procedural heightmap terrain mesh and texture
 */
class TerrainHeightmap {
public:
    TerrainHeightmap() = default;
    ~TerrainHeightmap() = default;

    /**
     * @brief Generate terrain mesh and heightmap texture
     * 
     * Creates a displaced plane mesh using GLM noise and matching GPU texture.
     * 
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for texture upload
     * @param graphicsQueue Graphics queue for texture upload
     * @param config Terrain configuration
     * @return true if successful
     */
    bool generate(VkDevice device, VmaAllocator allocator,
                  VkCommandPool commandPool, VkQueue graphicsQueue,
                  const TerrainConfig& config = TerrainConfig{});

    /**
     * @brief Add terrain to scene as renderable
     * 
     * @param sceneManager Scene manager to add to
     * @param materialSystem Material system for PBR material
     * @param allocator VMA allocator for material creation
     * @return Renderable handle
     */
    jupiter::rendering::RenderableHandle addToScene(
        jupiter::rendering::SceneManager* sceneManager,
        jupiter::rendering::MaterialSystem* materialSystem,
        VmaAllocator allocator);

    /**
     * @brief Get the generated mesh (for manual rendering)
     */
    jupiter::rendering::VulkanMesh* getMesh() { return mesh_.get(); }
    const jupiter::rendering::primitives::MeshData& getMeshData() const { return meshData_; }

    // Getters
    VkImageView getHeightmapView() const { return heightmapTexture_.getImageView(); }
    VkSampler getHeightmapSampler() const { return heightmapTexture_.getSampler(); }
    float getSize() const { return config_.size; }
    float getHeightScale() const { return config_.heightScale; }
    
    /**
     * @brief Sample height at world position (CPU-side, for testing)
     */
    float sampleHeight(float worldX, float worldZ) const;

private:
    TerrainConfig config_;
    jupiter::rendering::primitives::MeshData meshData_;
    std::unique_ptr<jupiter::rendering::VulkanMesh> mesh_;
    jupiter::rendering::VulkanTexture heightmapTexture_;
    std::vector<float> heightData_;  // CPU copy for queries

    float getNoiseHeight(float worldX, float worldZ) const;
};

} // namespace landscape

