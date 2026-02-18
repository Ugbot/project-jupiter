#pragma once

#include "material.h"
#include "assets/material_asset.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <unordered_map>
#include <string>

namespace jupiter {
namespace rendering {

/**
 * @brief Material system for managing PBR materials and descriptor sets
 *
 * Centralizes material descriptor set layout and pool management.
 * Provides high-level interface for creating materials from assets.
 *
 * Following CLAUDE.md:
 * - Pre-allocates descriptor pool during init
 * - No runtime allocations during material creation (uses pre-allocated pool)
 * - Lock-free (single-threaded material management)
 *
 * Descriptor Set Layout (Set 1 - Material):
 * - Binding 0: Base Color Texture (sampler2D)
 * - Binding 1: Normal Map (sampler2D)
 * - Binding 2: Metallic-Roughness Texture (sampler2D)
 * - Binding 3: Occlusion Texture (sampler2D)
 * - Binding 4: Emissive Texture (sampler2D)
 *
 * Benefits:
 * - Centralized material resource management
 * - Consistent descriptor set layout across all materials
 * - Easy material creation from assets
 * - Batch cleanup via pool reset
 *
 * Usage:
 * @code
 * MaterialSystem matSystem;
 * matSystem.initialize(device, 100);  // Max 100 materials
 *
 * Material* material = matSystem.createMaterial(materialAsset, textures);
 *
 * // Rendering
 * material->bind(commandBuffer, pipelineLayout, 1);
 * @endcode
 */
class MaterialSystem {
public:
    MaterialSystem();
    ~MaterialSystem();

    // Non-copyable
    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    /**
     * @brief Initialize material system
     *
     * Creates descriptor set layout and pool for materials.
     * Pre-allocates pool with capacity for maxMaterials.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator for UBO creation
     * @param maxMaterials Maximum number of materials (pre-allocates pool)
     * @return true if successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator, uint32_t maxMaterials = 100);

    /**
     * @brief Cleanup all material resources
     *
     * Destroys all materials, descriptor pool, and layout.
     */
    void destroy();

    /**
     * @brief Create material from CPU MaterialAsset
     *
     * Allocates descriptor set from pool and creates Material instance.
     * Material is owned by MaterialSystem and destroyed on cleanup.
     *
     * @param materialAsset CPU material asset data
     * @param textures Map of texture UUID -> VulkanTexture pointer
     * @return Pointer to created material (owned by system), nullptr on failure
     */
    Material* createMaterial(const assets::MaterialAsset& materialAsset,
                            const std::unordered_map<std::string, VulkanTexture*>& textures);

    /**
     * @brief Create material with UUID tracking
     *
     * Same as createMaterial() but stores material by UUID for lookup.
     *
     * @param uuid Material UUID for tracking
     * @param materialAsset CPU material asset data
     * @param textures Map of texture UUID -> VulkanTexture pointer
     * @return Pointer to created material (owned by system), nullptr on failure
     */
    Material* createMaterialWithUUID(const std::string& uuid,
                                     const assets::MaterialAsset& materialAsset,
                                     const std::unordered_map<std::string, VulkanTexture*>& textures);

    /**
     * @brief Get material by UUID
     *
     * @param uuid Material UUID
     * @return Pointer to material, nullptr if not found
     */
    Material* getMaterial(const std::string& uuid);

    /**
     * @brief Create a simple material from texture pointers
     *
     * Convenient method for creating materials from raw texture pointers,
     * useful for procedural geometry and debug rendering.
     *
     * @param albedo Base color texture (required)
     * @param normal Normal map (optional, uses default if null)
     * @param metallicRoughness Metallic/roughness texture (optional)
     * @param occlusion Ambient occlusion texture (optional)
     * @param emissive Emissive texture (optional)
     * @param baseColor Base color factor RGB (optional, default white)
     * @return Pointer to created material (owned by system), nullptr on failure
     */
    Material* createSimpleMaterial(VmaAllocator allocator,
                                   VulkanTexture* albedo,
                                   VulkanTexture* normal = nullptr,
                                   VulkanTexture* metallicRoughness = nullptr,
                                   VulkanTexture* occlusion = nullptr,
                                   VulkanTexture* emissive = nullptr,
                                   const float* baseColor = nullptr);

    /**
     * @brief Reset descriptor pool (destroys all materials)
     *
     * Resets descriptor pool, freeing all allocated descriptor sets.
     * All Material pointers become invalid after this call.
     */
    void resetPool();

    // Getters
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_; }
    VkDescriptorPool getDescriptorPool() const { return descriptorPool_; }
    uint32_t getMaterialCount() const { return static_cast<uint32_t>(materials_.size()); }

    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    // Material storage (owned by system)
    std::vector<Material*> materials_;

    // UUID -> Material mapping (optional, for fast lookup)
    std::unordered_map<std::string, Material*> materialsByUUID_;

    uint32_t maxMaterials_ = 0;

    // Helper: Create descriptor set layout
    bool createDescriptorSetLayout();

    // Helper: Create descriptor pool
    bool createDescriptorPool(uint32_t maxMaterials);
};

} // namespace rendering
} // namespace jupiter
