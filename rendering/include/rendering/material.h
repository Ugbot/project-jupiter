#pragma once

#include "assets/material_asset.h"
#include "texture.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <array>
#include <unordered_map>

namespace jupiter {
namespace rendering {
namespace vulkan {
class VulkanBuffer;
}

/**
 * @brief GPU material resource (descriptor sets + textures from CPU MaterialAsset)
 *
 * Wraps Vulkan descriptor sets for PBR material textures.
 * Separates CPU material properties from GPU resources.
 *
 * Following CLAUDE.md:
 * - No runtime allocations after creation
 * - Platform-agnostic asset input, GPU-specific output
 * - Lock-free (no shared state)
 *
 * PBR Textures (GLTF 2.0 Metallic-Roughness):
 * - Binding 0: Base Color (RGBA)
 * - Binding 1: Normal Map (RGB)
 * - Binding 2: Metallic-Roughness (G=roughness, B=metallic)
 * - Binding 3: Occlusion (R channel)
 * - Binding 4: Emissive (RGB)
 *
 * Material Uniform Buffer (updated per-material):
 * - Base color factor (vec4)
 * - Metallic/roughness factors (float, float)
 * - Emissive factor (vec3)
 * - Alpha mode/cutoff (uint, float)
 *
 * Benefits:
 * - Assets loaded without GPU
 * - Same asset creates multiple GPU materials (instances)
 * - Hot-reload: update textures without recreating descriptor sets
 *
 * Usage:
 * @code
 * MaterialAsset matAsset;
 * assetDB.getMaterialAsset(matUuid, matAsset);
 *
 * Material gpuMaterial;
 * gpuMaterial.createFromAsset(device, descriptorPool, layout, matAsset, textures);
 * @endcode
 */
class Material {
public:
    /**
     * @brief Material uniform buffer data (uploaded to GPU)
     *
     * Following GLTF 2.0 PBR Metallic-Roughness spec.
     * std140 layout alignment.
     */
    struct MaterialUBO {
        alignas(16) float baseColorFactor[4];   // RGBA
        alignas(16) float emissiveFactor[3];    // RGB
        alignas(4)  float metallicFactor;
        alignas(4)  float roughnessFactor;
        alignas(4)  float alphaCutoff;
        alignas(4)  uint32_t alphaMode;         // 0=OPAQUE, 1=MASK, 2=BLEND
        alignas(4)  uint32_t doubleSided;       // 0=false, 1=true
        alignas(4)  uint32_t hasBaseColorTex;   // Texture presence flags
        alignas(4)  uint32_t hasNormalTex;
        alignas(4)  uint32_t hasMetallicRoughnessTex;
        alignas(4)  uint32_t hasOcclusionTex;
        alignas(4)  uint32_t hasEmissiveTex;
    };

    Material();
    ~Material();

    // Non-copyable, movable
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;

    /**
     * @brief Create GPU material from CPU MaterialAsset
     *
     * Creates descriptor set for textures and stores material properties.
     * Textures must already be uploaded to GPU (VulkanTexture instances).
     *
     * @param device Vulkan device
     * @param descriptorPool Descriptor pool for allocation
     * @param descriptorSetLayout Material descriptor set layout (Set 1)
     * @param materialAsset CPU material asset data
     * @param textures Map of texture UUID -> VulkanTexture pointer
     * @return true if successful
     */
    bool createFromAsset(VkDevice device,
                        VkDescriptorPool descriptorPool,
                        VkDescriptorSetLayout descriptorSetLayout,
                        const assets::MaterialAsset& materialAsset,
                        const std::unordered_map<std::string, VulkanTexture*>& textures);

    /**
     * @brief Cleanup GPU resources
     *
     * Note: Descriptor sets are freed when pool is reset, don't free individually.
     * Textures are owned externally, not freed here.
     */
    void destroy();

    /**
     * @brief Create material directly from texture pointers (no asset needed)
     *
     * Simplified creation for procedural geometry and debug rendering.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator for UBO creation
     * @param descriptorPool Descriptor pool
     * @param descriptorSetLayout Material descriptor set layout
     * @param albedo Base color texture (required)
     * @param normal Normal map (optional)
     * @param metallicRoughness Metallic-roughness texture (optional)
     * @param occlusion Ambient occlusion texture (optional)
     * @param emissive Emissive texture (optional)
     * @param baseColor Base color factor (optional, default white)
     * @return true if successful
     */
    bool createSimple(VkDevice device,
                      VmaAllocator allocator,
                      VkDescriptorPool descriptorPool,
                      VkDescriptorSetLayout descriptorSetLayout,
                      VulkanTexture* albedo,
                      VulkanTexture* normal = nullptr,
                      VulkanTexture* metallicRoughness = nullptr,
                      VulkanTexture* occlusion = nullptr,
                      VulkanTexture* emissive = nullptr,
                      const float* baseColor = nullptr);
    
    /**
     * @brief Set base color factor
     * @param r Red (0-1)
     * @param g Green (0-1)  
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1)
     */
    void setBaseColor(float r, float g, float b, float a = 1.0f);
    
    /**
     * @brief Upload material properties to GPU UBO
     */
    void uploadProperties();

    /**
     * @brief Bind material descriptor set for rendering
     *
     * @param commandBuffer Command buffer
     * @param pipelineLayout Pipeline layout
     * @param setIndex Descriptor set index (typically 1 for materials)
     */
    void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
             uint32_t setIndex = 1) const;

    // Getters
    VkDescriptorSet getDescriptorSet() const { return descriptorSet_; }
    const MaterialUBO& getProperties() const { return properties_; }
    const std::string& getName() const { return name_; }

    bool isValid() const { return descriptorSet_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    // Material properties (CPU-side copy for updates)
    MaterialUBO properties_;
    std::string name_;
    
    // UBO buffer for material properties
    vulkan::VulkanBuffer* uboBuffer_ = nullptr;

    // Texture references (not owned, just pointers)
    VulkanTexture* baseColorTexture_ = nullptr;
    VulkanTexture* normalTexture_ = nullptr;
    VulkanTexture* metallicRoughnessTexture_ = nullptr;
    VulkanTexture* occlusionTexture_ = nullptr;
    VulkanTexture* emissiveTexture_ = nullptr;

    // Helper: Convert MaterialAsset to MaterialUBO
    void fillUBOFromAsset(const assets::MaterialAsset& materialAsset);
};

} // namespace rendering
} // namespace jupiter
