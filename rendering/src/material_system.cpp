#include "rendering/material_system.h"
#include "logging/logging.h"
#include <array>

namespace jupiter {
namespace rendering {

MaterialSystem::MaterialSystem() = default;

MaterialSystem::~MaterialSystem() {
    destroy();
}

bool MaterialSystem::initialize(VkDevice device, uint32_t maxMaterials) {
    if (device == VK_NULL_HANDLE || maxMaterials == 0) {
        return false;
    }

    destroy();

    device_ = device;
    maxMaterials_ = maxMaterials;

    // Create descriptor set layout
    if (!createDescriptorSetLayout()) {
        destroy();
        return false;
    }

    // Create descriptor pool
    if (!createDescriptorPool(maxMaterials)) {
        destroy();
        return false;
    }

    // Pre-allocate material storage (avoids runtime allocations)
    materials_.reserve(maxMaterials);

    return true;
}

void MaterialSystem::destroy() {
    // Destroy all materials
    for (Material* material : materials_) {
        if (material) {
            material->destroy();
            delete material;
        }
    }
    materials_.clear();
    materialsByUUID_.clear();

    // Destroy descriptor pool (frees all descriptor sets)
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
}

Material* MaterialSystem::createMaterial(const assets::MaterialAsset& materialAsset,
                                        const std::unordered_map<std::string, VulkanTexture*>& textures) {
    if (materials_.size() >= maxMaterials_) {
        return nullptr;  // Pool exhausted
    }

    Material* material = new Material();

    if (!material->createFromAsset(device_, descriptorPool_, descriptorSetLayout_,
                                   materialAsset, textures)) {
        delete material;
        return nullptr;
    }

    materials_.push_back(material);
    return material;
}

Material* MaterialSystem::createMaterialWithUUID(const std::string& uuid,
                                                 const assets::MaterialAsset& materialAsset,
                                                 const std::unordered_map<std::string, VulkanTexture*>& textures) {
    Material* material = createMaterial(materialAsset, textures);

    if (material && !uuid.empty()) {
        materialsByUUID_[uuid] = material;
    }

    return material;
}

Material* MaterialSystem::getMaterial(const std::string& uuid) {
    auto it = materialsByUUID_.find(uuid);
    return (it != materialsByUUID_.end()) ? it->second : nullptr;
}

Material* MaterialSystem::createSimpleMaterial(VmaAllocator allocator,
                                                VulkanTexture* albedo,
                                                VulkanTexture* normal,
                                                VulkanTexture* metallicRoughness,
                                                VulkanTexture* occlusion,
                                                VulkanTexture* emissive,
                                                const float* baseColor) {
    if (!albedo || device_ == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE) {
        return nullptr;
    }

    // Check pool capacity
    if (materials_.size() >= maxMaterials_) {
        LOG_WARN("MaterialSystem", "Material pool full (%u materials)", maxMaterials_);
        return nullptr;
    }

    // Create material using the simple creation method
    Material* material = new Material();
    
    if (!material->createSimple(device_, allocator, descriptorPool_, descriptorSetLayout_,
                                albedo, normal, metallicRoughness, occlusion, emissive, baseColor)) {
        LOG_ERROR("MaterialSystem", "Failed to create simple material");
        delete material;
        return nullptr;
    }

    materials_.push_back(material);
    return material;
}

void MaterialSystem::resetPool() {
    // Destroy all materials
    for (Material* material : materials_) {
        if (material) {
            material->destroy();
            delete material;
        }
    }
    materials_.clear();
    materialsByUUID_.clear();

    // Reset descriptor pool (frees all descriptor sets without destroying pool)
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device_, descriptorPool_, 0);
    }
}

bool MaterialSystem::createDescriptorSetLayout() {
    // PBR Material Descriptor Set Layout (Set 1)
    // - Binding 0: Base Color Texture
    // - Binding 1: Normal Map
    // - Binding 2: Metallic-Roughness Texture
    // - Binding 3: Occlusion Texture
    // - Binding 4: Emissive Texture
    // - Binding 5: Material Properties UBO

    std::array<VkDescriptorSetLayoutBinding, 6> bindings = {};

    // Texture bindings (0-4): combined image samplers in fragment shader
    for (uint32_t i = 0; i < 5; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[i].pImmutableSamplers = nullptr;
    }

    // Binding 5: Material UBO
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                   &descriptorSetLayout_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool MaterialSystem::createDescriptorPool(uint32_t maxMaterials) {
    // Each material has:
    // - 5 texture samplers
    // - 1 UBO for material properties
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = maxMaterials * 5;  // 5 textures per material
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = maxMaterials;  // 1 UBO per material

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxMaterials;  // Max descriptor sets
    poolInfo.flags = 0;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

} // namespace rendering
} // namespace jupiter
