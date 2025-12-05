#include "rendering/material.h"
#include "rendering/default_textures.h"
#include "vulkan_backend.h"
#include "logging/logging.h"
#include <cstring>
#include <array>

namespace jupiter {
namespace rendering {

Material::Material() {
    // Initialize properties to default PBR values
    std::memset(&properties_, 0, sizeof(properties_));
    properties_.baseColorFactor[0] = 1.0f;
    properties_.baseColorFactor[1] = 1.0f;
    properties_.baseColorFactor[2] = 1.0f;
    properties_.baseColorFactor[3] = 1.0f;
    properties_.metallicFactor = 1.0f;
    properties_.roughnessFactor = 1.0f;
    properties_.alphaCutoff = 0.5f;
}

Material::~Material() {
    destroy();
}

Material::Material(Material&& other) noexcept
    : device_(other.device_)
    , descriptorSet_(other.descriptorSet_)
    , properties_(other.properties_)
    , name_(std::move(other.name_))
    , baseColorTexture_(other.baseColorTexture_)
    , normalTexture_(other.normalTexture_)
    , metallicRoughnessTexture_(other.metallicRoughnessTexture_)
    , occlusionTexture_(other.occlusionTexture_)
    , emissiveTexture_(other.emissiveTexture_) {

    other.device_ = VK_NULL_HANDLE;
    other.descriptorSet_ = VK_NULL_HANDLE;
    other.baseColorTexture_ = nullptr;
    other.normalTexture_ = nullptr;
    other.metallicRoughnessTexture_ = nullptr;
    other.occlusionTexture_ = nullptr;
    other.emissiveTexture_ = nullptr;
}

Material& Material::operator=(Material&& other) noexcept {
    if (this != &other) {
        destroy();

        device_ = other.device_;
        descriptorSet_ = other.descriptorSet_;
        properties_ = other.properties_;
        name_ = std::move(other.name_);
        baseColorTexture_ = other.baseColorTexture_;
        normalTexture_ = other.normalTexture_;
        metallicRoughnessTexture_ = other.metallicRoughnessTexture_;
        occlusionTexture_ = other.occlusionTexture_;
        emissiveTexture_ = other.emissiveTexture_;

        other.device_ = VK_NULL_HANDLE;
        other.descriptorSet_ = VK_NULL_HANDLE;
        other.baseColorTexture_ = nullptr;
        other.normalTexture_ = nullptr;
        other.metallicRoughnessTexture_ = nullptr;
        other.occlusionTexture_ = nullptr;
        other.emissiveTexture_ = nullptr;
    }
    return *this;
}

bool Material::createFromAsset(VkDevice device,
                               VkDescriptorPool descriptorPool,
                               VkDescriptorSetLayout descriptorSetLayout,
                               const assets::MaterialAsset& materialAsset,
                               const std::unordered_map<std::string, VulkanTexture*>& textures) {
    if (device == VK_NULL_HANDLE || descriptorPool == VK_NULL_HANDLE ||
        descriptorSetLayout == VK_NULL_HANDLE) {
        return false;
    }

    destroy();

    device_ = device;
    name_ = materialAsset.name;

    // Fill material properties from asset
    fillUBOFromAsset(materialAsset);

    // Lookup textures by UUID
    auto findTexture = [&textures](const std::string& uuid) -> VulkanTexture* {
        if (uuid.empty()) return nullptr;
        auto it = textures.find(uuid);
        return (it != textures.end()) ? it->second : nullptr;
    };

    // Lookup textures by UUID, then substitute defaults for missing slots
    VulkanTexture* foundBaseColor = findTexture(materialAsset.baseColorTextureUuid);
    VulkanTexture* foundNormal = findTexture(materialAsset.normalTextureUuid);
    VulkanTexture* foundMetallicRoughness = findTexture(materialAsset.metallicRoughnessTextureUuid);
    VulkanTexture* foundOcclusion = findTexture(materialAsset.occlusionTextureUuid);
    VulkanTexture* foundEmissive = findTexture(materialAsset.emissiveTextureUuid);

    // Update texture presence flags (before substituting defaults)
    properties_.hasBaseColorTex = (foundBaseColor != nullptr) ? 1 : 0;
    properties_.hasNormalTex = (foundNormal != nullptr) ? 1 : 0;
    properties_.hasMetallicRoughnessTex = (foundMetallicRoughness != nullptr) ? 1 : 0;
    properties_.hasOcclusionTex = (foundOcclusion != nullptr) ? 1 : 0;
    properties_.hasEmissiveTex = (foundEmissive != nullptr) ? 1 : 0;

    // Substitute default textures for missing slots (ensures all bindings are valid)
    auto& defaults = DefaultTextures::get();
    baseColorTexture_ = foundBaseColor ? foundBaseColor : defaults.getWhiteTexture();
    normalTexture_ = foundNormal ? foundNormal : defaults.getNormalTexture();
    metallicRoughnessTexture_ = foundMetallicRoughness ? foundMetallicRoughness : defaults.getBlackTexture();
    occlusionTexture_ = foundOcclusion ? foundOcclusion : defaults.getWhiteTexture();
    emissiveTexture_ = foundEmissive ? foundEmissive : defaults.getBlackTexture();

    LOG_INFO("Material", "Texture assignment for '%s':", materialAsset.name.c_str());
    LOG_INFO("Material", "  baseColor: %s -> %p%s",
             materialAsset.baseColorTextureUuid.c_str(), baseColorTexture_,
             foundBaseColor ? "" : " (default)");
    LOG_INFO("Material", "  normal: %s -> %p%s",
             materialAsset.normalTextureUuid.c_str(), normalTexture_,
             foundNormal ? "" : " (default)");
    LOG_INFO("Material", "  metallicRoughness: %s -> %p%s",
             materialAsset.metallicRoughnessTextureUuid.c_str(), metallicRoughnessTexture_,
             foundMetallicRoughness ? "" : " (default)");
    LOG_INFO("Material", "  occlusion: %s -> %p%s",
             materialAsset.occlusionTextureUuid.c_str(), occlusionTexture_,
             foundOcclusion ? "" : " (default)");
    LOG_INFO("Material", "  emissive: %s -> %p%s",
             materialAsset.emissiveTextureUuid.c_str(), emissiveTexture_,
             foundEmissive ? "" : " (default)");

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        return false;
    }

    // Update descriptor set with textures (all 5 slots always bound)
    std::array<VkDescriptorImageInfo, 5> imageInfos;
    std::array<VkWriteDescriptorSet, 5> descriptorWrites = {};

    // Helper to add a texture binding (textures are guaranteed non-null due to defaults)
    auto addTextureBinding = [&](VulkanTexture* texture, uint32_t binding) {
        imageInfos[binding].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[binding].imageView = texture->getImageView();
        imageInfos[binding].sampler = texture->getSampler();

        descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[binding].dstSet = descriptorSet_;
        descriptorWrites[binding].dstBinding = binding;
        descriptorWrites[binding].dstArrayElement = 0;
        descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[binding].descriptorCount = 1;
        descriptorWrites[binding].pImageInfo = &imageInfos[binding];
    };

    LOG_INFO("Material", "Binding textures to descriptor set:");
    addTextureBinding(baseColorTexture_, 0);
    LOG_INFO("Material", "  Binding 0 (albedo): %p", baseColorTexture_);

    addTextureBinding(normalTexture_, 1);
    LOG_INFO("Material", "  Binding 1 (normal): %p", normalTexture_);

    addTextureBinding(metallicRoughnessTexture_, 2);
    LOG_INFO("Material", "  Binding 2 (metalRough): %p", metallicRoughnessTexture_);

    addTextureBinding(occlusionTexture_, 3);
    LOG_INFO("Material", "  Binding 3 (occlusion): %p", occlusionTexture_);

    addTextureBinding(emissiveTexture_, 4);
    LOG_INFO("Material", "  Binding 4 (emissive): %p", emissiveTexture_);

    // Always update all 5 descriptor bindings
    vkUpdateDescriptorSets(device_, 5, descriptorWrites.data(), 0, nullptr);
    LOG_INFO("Material", "Updated 5 descriptor bindings (all slots bound)");

    return true;
}

void Material::destroy() {
    // Clean up UBO buffer
    if (uboBuffer_) {
        uboBuffer_->destroy();
        delete uboBuffer_;
        uboBuffer_ = nullptr;
    }
    
    // Note: Descriptor sets are freed when pool is reset, don't free individually
    descriptorSet_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;

    // Textures are owned externally, don't delete
    baseColorTexture_ = nullptr;
    normalTexture_ = nullptr;
    metallicRoughnessTexture_ = nullptr;
    occlusionTexture_ = nullptr;
    emissiveTexture_ = nullptr;
}

bool Material::createSimple(VkDevice device,
                            VmaAllocator allocator,
                            VkDescriptorPool descriptorPool,
                            VkDescriptorSetLayout descriptorSetLayout,
                            VulkanTexture* albedo,
                            VulkanTexture* normal,
                            VulkanTexture* metallicRoughness,
                            VulkanTexture* occlusion,
                            VulkanTexture* emissive,
                            const float* baseColor) {
    if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE ||
        descriptorPool == VK_NULL_HANDLE || descriptorSetLayout == VK_NULL_HANDLE || !albedo) {
        return false;
    }

    destroy();

    device_ = device;
    allocator_ = allocator;
    name_ = "simple_material";

    // Set default properties for a simple material
    if (baseColor) {
        properties_.baseColorFactor[0] = baseColor[0];
        properties_.baseColorFactor[1] = baseColor[1];
        properties_.baseColorFactor[2] = baseColor[2];
        properties_.baseColorFactor[3] = 1.0f;
    } else {
        properties_.baseColorFactor[0] = 1.0f;
        properties_.baseColorFactor[1] = 1.0f;
        properties_.baseColorFactor[2] = 1.0f;
        properties_.baseColorFactor[3] = 1.0f;
    }
    properties_.metallicFactor = 0.0f;
    properties_.roughnessFactor = 0.8f;
    properties_.alphaCutoff = 0.5f;
    properties_.alphaMode = 0;  // OPAQUE
    properties_.doubleSided = 0;
    properties_.emissiveFactor[0] = 0.0f;
    properties_.emissiveFactor[1] = 0.0f;
    properties_.emissiveFactor[2] = 0.0f;

    // Substitute defaults for missing textures
    auto& defaults = DefaultTextures::get();
    baseColorTexture_ = albedo;
    normalTexture_ = normal ? normal : defaults.getNormalTexture();
    metallicRoughnessTexture_ = metallicRoughness ? metallicRoughness : defaults.getBlackTexture();
    occlusionTexture_ = occlusion ? occlusion : defaults.getWhiteTexture();
    emissiveTexture_ = emissive ? emissive : defaults.getBlackTexture();

    // Update presence flags
    properties_.hasBaseColorTex = 1;
    properties_.hasNormalTex = (normal != nullptr) ? 1 : 0;
    properties_.hasMetallicRoughnessTex = (metallicRoughness != nullptr) ? 1 : 0;
    properties_.hasOcclusionTex = (occlusion != nullptr) ? 1 : 0;
    properties_.hasEmissiveTex = (emissive != nullptr) ? 1 : 0;

    // Create UBO buffer for material properties
    uboBuffer_ = new vulkan::VulkanBuffer();
    if (!uboBuffer_->create(allocator_, sizeof(MaterialUBO), 
                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VMA_MEMORY_USAGE_CPU_TO_GPU)) {
        LOG_ERROR("Material", "Failed to create material UBO buffer");
        delete uboBuffer_;
        uboBuffer_ = nullptr;
        return false;
    }
    
    // Upload initial properties
    uboBuffer_->upload(&properties_, sizeof(properties_));

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        LOG_ERROR("Material", "Failed to allocate descriptor set");
        return false;
    }

    // Update descriptor set with textures (bindings 0-4) and UBO (binding 5)
    std::array<VkDescriptorImageInfo, 5> imageInfos;
    std::array<VkWriteDescriptorSet, 6> descriptorWrites = {};

    auto addTextureBinding = [&](VulkanTexture* texture, uint32_t binding) {
        imageInfos[binding].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[binding].imageView = texture->getImageView();
        imageInfos[binding].sampler = texture->getSampler();

        descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[binding].dstSet = descriptorSet_;
        descriptorWrites[binding].dstBinding = binding;
        descriptorWrites[binding].dstArrayElement = 0;
        descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[binding].descriptorCount = 1;
        descriptorWrites[binding].pImageInfo = &imageInfos[binding];
    };

    addTextureBinding(baseColorTexture_, 0);
    addTextureBinding(normalTexture_, 1);
    addTextureBinding(metallicRoughnessTexture_, 2);
    addTextureBinding(occlusionTexture_, 3);
    addTextureBinding(emissiveTexture_, 4);

    // Binding 5: Material UBO
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = uboBuffer_->getBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(MaterialUBO);
    
    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = descriptorSet_;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device_, 6, descriptorWrites.data(), 0, nullptr);

    return true;
}

void Material::setBaseColor(float r, float g, float b, float a) {
    properties_.baseColorFactor[0] = r;
    properties_.baseColorFactor[1] = g;
    properties_.baseColorFactor[2] = b;
    properties_.baseColorFactor[3] = a;
    uploadProperties();
}

void Material::uploadProperties() {
    if (uboBuffer_) {
        uboBuffer_->upload(&properties_, sizeof(properties_));
    }
}

void Material::bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                   uint32_t setIndex) const {
    if (descriptorSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               pipelineLayout, setIndex, 1, &descriptorSet_,
                               0, nullptr);
    }
}

void Material::fillUBOFromAsset(const assets::MaterialAsset& materialAsset) {
    // Base color factor
    std::memcpy(properties_.baseColorFactor, materialAsset.baseColorFactor,
               sizeof(properties_.baseColorFactor));

    // Metallic/roughness
    properties_.metallicFactor = materialAsset.metallicFactor;
    properties_.roughnessFactor = materialAsset.roughnessFactor;

    // Emissive factor
    std::memcpy(properties_.emissiveFactor, materialAsset.emissiveFactor,
               sizeof(properties_.emissiveFactor));

    // Alpha mode
    if (materialAsset.alphaMode == assets::MaterialAsset::AlphaMode::OPAQUE) {
        properties_.alphaMode = 0;
    } else if (materialAsset.alphaMode == assets::MaterialAsset::AlphaMode::MASK) {
        properties_.alphaMode = 1;
    } else { // BLEND
        properties_.alphaMode = 2;
    }

    properties_.alphaCutoff = materialAsset.alphaCutoff;
    properties_.doubleSided = materialAsset.doubleSided ? 1 : 0;
}

} // namespace rendering
} // namespace jupiter
