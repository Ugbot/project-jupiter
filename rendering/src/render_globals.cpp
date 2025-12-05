#include "rendering/render_globals.h"
#include "rendering/default_textures.h"
#include "rendering/texture.h"
#include "vulkan_backend.h"
#include "logging/logging.h"
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

namespace jupiter {
namespace rendering {

RenderGlobals::RenderGlobals() = default;

RenderGlobals::~RenderGlobals() {
    destroy();
}

bool RenderGlobals::initialize(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight) {
    LOG_INFO("RenderGlobals", "Initializing with device=%p, allocator=%p, frames=%u",
             device, allocator, framesInFlight);

    if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE || framesInFlight == 0) {
        LOG_ERROR("RenderGlobals", "Invalid parameters: device=%p, allocator=%p, frames=%u",
                  device, allocator, framesInFlight);
        return false;
    }

    // Validate allocator by getting its info
    LOG_INFO("RenderGlobals", "Validating VMA allocator...");
    VmaAllocatorInfo allocatorInfo;
    vmaGetAllocatorInfo(allocator, &allocatorInfo);
    LOG_INFO("RenderGlobals", "VMA allocator info: device=%p, instance=%p",
             allocatorInfo.device, allocatorInfo.instance);

    if (allocatorInfo.device != device) {
        LOG_ERROR("RenderGlobals", "VMA allocator device mismatch: expected %p, got %p",
                  device, allocatorInfo.device);
        return false;
    }

    destroy();

    device_ = device;
    allocator_ = allocator;
    framesInFlight_ = framesInFlight;

    LOG_INFO("RenderGlobals", "Creating descriptor set layout...");

    // Create descriptor set layout
    if (!createDescriptorSetLayout()) {
        destroy();
        return false;
    }

    // Create descriptor pool
    if (!createDescriptorPool()) {
        destroy();
        return false;
    }

    // Create uniform buffers (per-frame)
    if (!createUniformBuffers()) {
        destroy();
        return false;
    }

    // Create and update descriptor sets
    if (!createDescriptorSets()) {
        destroy();
        return false;
    }

    return true;
}

void RenderGlobals::destroy() {
    // Destroy uniform buffers
    for (auto* buffer : cameraBuffers_) {
        if (buffer) {
            buffer->destroy();
            delete buffer;
        }
    }
    cameraBuffers_.clear();

    for (auto* buffer : lightingBuffers_) {
        if (buffer) {
            buffer->destroy();
            delete buffer;
        }
    }
    lightingBuffers_.clear();

    for (auto* buffer : shadowEffectsBuffers_) {
        if (buffer) {
            buffer->destroy();
            delete buffer;
        }
    }
    shadowEffectsBuffers_.clear();

    // Destroy descriptor pool (frees descriptor sets)
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    descriptorSets_.clear();

    shadowMapBound_ = false;
    ssaoTextureBound_ = false;
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    framesInFlight_ = 0;
}

bool RenderGlobals::updateCamera(uint32_t frameIndex, const Camera& camera) {
    if (frameIndex >= framesInFlight_ || !cameraBuffers_[frameIndex]) {
        return false;
    }

    CameraUBO ubo;

    // Get camera matrices (Matrix4x4 wraps glm::mat4, use .get() to access)
    const glm::mat4& view = camera.getViewMatrix().get();
    const glm::mat4& projection = camera.getProjectionMatrix().get();

    // Copy matrices (glm is column-major, same as GLSL)
    std::memcpy(ubo.view, glm::value_ptr(view), sizeof(ubo.view));
    std::memcpy(ubo.projection, glm::value_ptr(projection), sizeof(ubo.projection));

    // Camera position (Vector3 has public x, y, z members)
    const math::Vector3& position = camera.getPosition();
    ubo.cameraPosition[0] = position.x;
    ubo.cameraPosition[1] = position.y;
    ubo.cameraPosition[2] = position.z;

    // Upload to GPU
    return cameraBuffers_[frameIndex]->upload(&ubo, sizeof(ubo));
}

bool RenderGlobals::updateLights(uint32_t frameIndex, const LightManager& lightManager) {
    if (frameIndex >= framesInFlight_ || !lightingBuffers_[frameIndex]) {
        return false;
    }

    LightingUBO ubo;
    std::memset(&ubo, 0, sizeof(ubo));

    // Default ambient color (low gray, intensity 1.0)
    ubo.ambientColor[0] = 0.03f;
    ubo.ambientColor[1] = 0.03f;
    ubo.ambientColor[2] = 0.03f;
    ubo.ambientColor[3] = 1.0f;

    // Pack complex Light structures into simple vec4 format for shader
    const Light* lights = lightManager.getLights();
    uint32_t lightCount = lightManager.getLightCount();
    uint32_t uploadedLightCount = 0;

    for (uint32_t i = 0; i < lightCount && uploadedLightCount < MAX_LIGHTS; i++) {
        const Light& light = lights[i];

        switch (static_cast<LightType>(light.type)) {
            case LightType::DIRECTIONAL:
                // Position/Direction vec4: xyz = direction (TOWARDS light), w = type (0)
                ubo.lightPositions[uploadedLightCount][0] = light.directional.direction[0];
                ubo.lightPositions[uploadedLightCount][1] = light.directional.direction[1];
                ubo.lightPositions[uploadedLightCount][2] = light.directional.direction[2];
                ubo.lightPositions[uploadedLightCount][3] = 0.0f;  // type = 0 (directional)

                // Color vec4: xyz = color, w = intensity
                ubo.lightColors[uploadedLightCount][0] = light.directional.color[0];
                ubo.lightColors[uploadedLightCount][1] = light.directional.color[1];
                ubo.lightColors[uploadedLightCount][2] = light.directional.color[2];
                ubo.lightColors[uploadedLightCount][3] = light.directional.intensity;
                uploadedLightCount++;
                break;

            case LightType::POINT:
                // Position/Direction vec4: xyz = position, w = type (1)
                ubo.lightPositions[uploadedLightCount][0] = light.point.position[0];
                ubo.lightPositions[uploadedLightCount][1] = light.point.position[1];
                ubo.lightPositions[uploadedLightCount][2] = light.point.position[2];
                ubo.lightPositions[uploadedLightCount][3] = 1.0f;  // type = 1 (point)

                // Color vec4: xyz = color, w = intensity
                ubo.lightColors[uploadedLightCount][0] = light.point.color[0];
                ubo.lightColors[uploadedLightCount][1] = light.point.color[1];
                ubo.lightColors[uploadedLightCount][2] = light.point.color[2];
                ubo.lightColors[uploadedLightCount][3] = light.point.intensity;
                uploadedLightCount++;
                break;

            case LightType::SPOT:
                // Spot lights treated as point lights for now (simplified)
                ubo.lightPositions[uploadedLightCount][0] = light.spot.position[0];
                ubo.lightPositions[uploadedLightCount][1] = light.spot.position[1];
                ubo.lightPositions[uploadedLightCount][2] = light.spot.position[2];
                ubo.lightPositions[uploadedLightCount][3] = 1.0f;  // type = 1 (point)

                ubo.lightColors[uploadedLightCount][0] = light.spot.color[0];
                ubo.lightColors[uploadedLightCount][1] = light.spot.color[1];
                ubo.lightColors[uploadedLightCount][2] = light.spot.color[2];
                ubo.lightColors[uploadedLightCount][3] = light.spot.intensity;
                uploadedLightCount++;
                break;

            case LightType::AMBIENT:
                // Upload ambient light to dedicated ambientColor field
                ubo.ambientColor[0] = light.ambient.color[0];
                ubo.ambientColor[1] = light.ambient.color[1];
                ubo.ambientColor[2] = light.ambient.color[2];
                ubo.ambientColor[3] = light.ambient.intensity;
                // Ambient lights don't count toward uploadedLightCount (not in light array)
                break;
        }
    }

    ubo.lightCount = uploadedLightCount;

    // Upload to GPU
    return lightingBuffers_[frameIndex]->upload(&ubo, sizeof(ubo));
}

bool RenderGlobals::updateIBLResources(VkImageView irradianceView, VkSampler irradianceSampler,
                                       VkImageView prefilteredView, VkSampler prefilteredSampler,
                                       VkImageView brdfLUTView, VkSampler brdfLUTSampler) {
    if (device_ == VK_NULL_HANDLE || descriptorSets_.empty()) {
        LOG_ERROR("RenderGlobals", "Cannot update IBL resources: not initialized");
        return false;
    }

    if (irradianceView == VK_NULL_HANDLE || irradianceSampler == VK_NULL_HANDLE ||
        prefilteredView == VK_NULL_HANDLE || prefilteredSampler == VK_NULL_HANDLE ||
        brdfLUTView == VK_NULL_HANDLE || brdfLUTSampler == VK_NULL_HANDLE) {
        LOG_ERROR("RenderGlobals", "Cannot update IBL resources: invalid handles");
        return false;
    }

    LOG_INFO("RenderGlobals", "Updating IBL resources for %u descriptor sets", framesInFlight_);

    // Update all descriptor sets with the same IBL samplers (shared across frames)
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        VkDescriptorImageInfo irradianceInfo = {};
        irradianceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irradianceInfo.imageView = irradianceView;
        irradianceInfo.sampler = irradianceSampler;

        VkDescriptorImageInfo prefilteredInfo = {};
        prefilteredInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefilteredInfo.imageView = prefilteredView;
        prefilteredInfo.sampler = prefilteredSampler;

        VkDescriptorImageInfo brdfLUTInfo = {};
        brdfLUTInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        brdfLUTInfo.imageView = brdfLUTView;
        brdfLUTInfo.sampler = brdfLUTSampler;

        VkWriteDescriptorSet writes[3] = {};

        // Binding 2: Irradiance map
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = 2;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &irradianceInfo;

        // Binding 3: Prefiltered environment map
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = 3;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &prefilteredInfo;

        // Binding 4: BRDF LUT
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets_[i];
        writes[2].dstBinding = 4;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &brdfLUTInfo;

        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
    }

    LOG_INFO("RenderGlobals", "IBL resources updated successfully");
    return true;
}

VkDescriptorSet RenderGlobals::getDescriptorSet(uint32_t frameIndex) const {
    if (frameIndex >= descriptorSets_.size()) {
        return VK_NULL_HANDLE;
    }
    return descriptorSets_[frameIndex];
}

bool RenderGlobals::createDescriptorSetLayout() {
    // Set 0 layout: Camera UBO + Lighting UBO + IBL samplers + Shadow/SSAO
    VkDescriptorSetLayoutBinding bindings[8] = {};

    // Binding 0: Camera UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Lighting UBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 2: Irradiance map (samplerCube)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    // Binding 3: Prefiltered environment map (samplerCube)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].pImmutableSamplers = nullptr;

    // Binding 4: BRDF LUT (sampler2D)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].pImmutableSamplers = nullptr;

    // Binding 5: Shadow map (sampler2D with depth comparison)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].pImmutableSamplers = nullptr;

    // Binding 6: SSAO texture (sampler2D)
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[6].pImmutableSamplers = nullptr;

    // Binding 7: Shadow/Effects UBO
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[7].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 8;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                   &descriptorSetLayout_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool RenderGlobals::createDescriptorPool() {
    // Pool for per-frame descriptor sets
    // Need space for: 3 UBOs + 5 samplers per frame
    VkDescriptorPoolSize poolSizes[2] = {};

    // UBOs: Camera + Lighting + ShadowEffects = 3 per frame
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = framesInFlight_ * 3;

    // Samplers: Irradiance + Prefiltered + BRDF LUT + Shadow + SSAO = 5 per frame
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = framesInFlight_ * 5;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = framesInFlight_;
    poolInfo.flags = 0;  // Standard pool, bindings updated when textures are created

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool RenderGlobals::createUniformBuffers() {
    LOG_INFO("RenderGlobals", "Creating uniform buffers for %u frames", framesInFlight_);
    LOG_INFO("RenderGlobals", "CameraUBO size: %zu bytes", sizeof(CameraUBO));
    LOG_INFO("RenderGlobals", "LightingUBO size: %zu bytes", sizeof(LightingUBO));
    LOG_INFO("RenderGlobals", "ShadowEffectsUBO size: %zu bytes", sizeof(ShadowEffectsUBO));

    cameraBuffers_.resize(framesInFlight_);
    lightingBuffers_.resize(framesInFlight_);
    shadowEffectsBuffers_.resize(framesInFlight_);

    for (uint32_t i = 0; i < framesInFlight_; i++) {
        LOG_INFO("RenderGlobals", "Creating buffers for frame %u/%u", i + 1, framesInFlight_);

        // Create camera UBO
        LOG_INFO("RenderGlobals", "Creating camera UBO (frame %u)...", i);
        cameraBuffers_[i] = new vulkan::VulkanBuffer();
        if (!cameraBuffers_[i]->create(allocator_, sizeof(CameraUBO),
                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VMA_MEMORY_USAGE_CPU_TO_GPU)) {
            LOG_ERROR("RenderGlobals", "Failed to create camera UBO for frame %u", i);
            return false;
        }
        LOG_INFO("RenderGlobals", "Created camera UBO for frame %u", i);

        // Create lighting UBO
        LOG_INFO("RenderGlobals", "Creating lighting UBO (frame %u)...", i);
        lightingBuffers_[i] = new vulkan::VulkanBuffer();
        if (!lightingBuffers_[i]->create(allocator_, sizeof(LightingUBO),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VMA_MEMORY_USAGE_CPU_TO_GPU)) {
            LOG_ERROR("RenderGlobals", "Failed to create lighting UBO for frame %u", i);
            return false;
        }
        LOG_INFO("RenderGlobals", "Created lighting UBO for frame %u", i);

        // Create shadow effects UBO
        LOG_INFO("RenderGlobals", "Creating shadow effects UBO (frame %u)...", i);
        shadowEffectsBuffers_[i] = new vulkan::VulkanBuffer();
        if (!shadowEffectsBuffers_[i]->create(allocator_, sizeof(ShadowEffectsUBO),
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                             VMA_MEMORY_USAGE_CPU_TO_GPU)) {
            LOG_ERROR("RenderGlobals", "Failed to create shadow effects UBO for frame %u", i);
            return false;
        }
        
        // Initialize shadow effects UBO with disabled state
        ShadowEffectsUBO defaultEffects{};
        defaultEffects.lightSpaceMatrix = glm::mat4(1.0f);
        defaultEffects.shadowParams = glm::vec4(0.005f, 0.02f, 1.0f, 0.0f);
        defaultEffects.shadowEnabled = 0;
        defaultEffects.ssaoEnabled = 0;
        defaultEffects.ssaoIntensity = 1.0f;
        shadowEffectsBuffers_[i]->upload(&defaultEffects, sizeof(ShadowEffectsUBO));
        
        LOG_INFO("RenderGlobals", "Created shadow effects UBO for frame %u", i);
    }

    LOG_INFO("RenderGlobals", "Successfully created all uniform buffers");
    return true;
}

bool RenderGlobals::createDescriptorSets() {
    descriptorSets_.resize(framesInFlight_);

    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight_, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = framesInFlight_;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        return false;
    }

    // Update descriptor sets with buffer bindings
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        VkDescriptorBufferInfo cameraBufferInfo = {};
        cameraBufferInfo.buffer = cameraBuffers_[i]->getBuffer();
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraUBO);

        VkDescriptorBufferInfo lightingBufferInfo = {};
        lightingBufferInfo.buffer = lightingBuffers_[i]->getBuffer();
        lightingBufferInfo.offset = 0;
        lightingBufferInfo.range = sizeof(LightingUBO);

        VkDescriptorBufferInfo shadowEffectsBufferInfo = {};
        shadowEffectsBufferInfo.buffer = shadowEffectsBuffers_[i]->getBuffer();
        shadowEffectsBufferInfo.offset = 0;
        shadowEffectsBufferInfo.range = sizeof(ShadowEffectsUBO);

        VkWriteDescriptorSet writes[3] = {};

        // Camera UBO (binding 0)
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraBufferInfo;

        // Lighting UBO (binding 1)
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightingBufferInfo;

        // Shadow Effects UBO (binding 7)
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets_[i];
        writes[2].dstBinding = 7;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &shadowEffectsBufferInfo;

        vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);

        // Initialize shadow map (binding 5) and SSAO (binding 6) with placeholder textures
        // Real textures will be bound when features are enabled
        auto& defaults = DefaultTextures::get();
        if (defaults.isInitialized()) {
            VulkanTexture* placeholder = defaults.getWhiteTexture();
            if (placeholder && placeholder->getImageView() != VK_NULL_HANDLE && 
                placeholder->getSampler() != VK_NULL_HANDLE) {
                
                VkDescriptorImageInfo placeholderInfo = {};
                placeholderInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                placeholderInfo.imageView = placeholder->getImageView();
                placeholderInfo.sampler = placeholder->getSampler();
                
                VkWriteDescriptorSet placeholderWrites[2] = {};
                
                // Binding 5: Shadow map placeholder
                placeholderWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                placeholderWrites[0].dstSet = descriptorSets_[i];
                placeholderWrites[0].dstBinding = 5;
                placeholderWrites[0].dstArrayElement = 0;
                placeholderWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                placeholderWrites[0].descriptorCount = 1;
                placeholderWrites[0].pImageInfo = &placeholderInfo;

                // Binding 6: SSAO placeholder
                placeholderWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                placeholderWrites[1].dstSet = descriptorSets_[i];
                placeholderWrites[1].dstBinding = 6;
                placeholderWrites[1].dstArrayElement = 0;
                placeholderWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                placeholderWrites[1].descriptorCount = 1;
                placeholderWrites[1].pImageInfo = &placeholderInfo;

                vkUpdateDescriptorSets(device_, 2, placeholderWrites, 0, nullptr);
            }
        }
    }

    return true;
}

bool RenderGlobals::updateShadowMap(VkImageView shadowMapView, VkSampler shadowSampler) {
    if (!device_ || descriptorSets_.empty()) return false;

    LOG_INFO("RenderGlobals", "Binding shadow map to descriptor sets");

    for (uint32_t i = 0; i < framesInFlight_; i++) {
        VkDescriptorImageInfo shadowMapInfo = {};
        shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowMapInfo.imageView = shadowMapView;
        shadowMapInfo.sampler = shadowSampler;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[i];
        write.dstBinding = 5;  // Shadow map binding
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &shadowMapInfo;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    shadowMapBound_ = true;
    LOG_INFO("RenderGlobals", "Shadow map bound successfully");
    return true;
}

bool RenderGlobals::updateSSAOTexture(VkImageView ssaoView, VkSampler ssaoSampler) {
    if (!device_ || descriptorSets_.empty()) return false;

    LOG_INFO("RenderGlobals", "Binding SSAO texture to descriptor sets");

    for (uint32_t i = 0; i < framesInFlight_; i++) {
        VkDescriptorImageInfo ssaoInfo = {};
        ssaoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ssaoInfo.imageView = ssaoView;
        ssaoInfo.sampler = ssaoSampler;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[i];
        write.dstBinding = 6;  // SSAO binding
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &ssaoInfo;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    ssaoTextureBound_ = true;
    LOG_INFO("RenderGlobals", "SSAO texture bound successfully");
    return true;
}

bool RenderGlobals::updateShadowEffects(uint32_t frameIndex,
                                        const glm::mat4& lightSpaceMatrix,
                                        bool shadowEnabled,
                                        bool ssaoEnabled,
                                        float ssaoIntensity,
                                        float shadowBias) {
    if (frameIndex >= framesInFlight_ || !shadowEffectsBuffers_[frameIndex]) {
        return false;
    }

    ShadowEffectsUBO ubo{};
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    ubo.shadowParams = glm::vec4(shadowBias, 0.02f, 1.0f, 0.0f);
    ubo.shadowEnabled = shadowEnabled && shadowMapBound_ ? 1 : 0;
    ubo.ssaoEnabled = ssaoEnabled && ssaoTextureBound_ ? 1 : 0;
    ubo.ssaoIntensity = ssaoIntensity;

    shadowEffectsBuffers_[frameIndex]->upload(&ubo, sizeof(ShadowEffectsUBO));
    return true;
}

} // namespace rendering
} // namespace jupiter
