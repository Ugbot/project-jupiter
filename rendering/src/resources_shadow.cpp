/**
 * @file resources_shadow.cpp
 * @brief Shadow mapping resources implementation
 */

#include "rendering/resources_shadow.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>  // For glm::orthoRH_ZO (Vulkan Z range)
#include <cstring>
#include <stdexcept>
#include <cmath>

namespace jupiter::rendering {

namespace {

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

} // anonymous namespace

ResourcesShadow::~ResourcesShadow() {
    destroy();
}

void ResourcesShadow::create(VkDevice device,
                              VkPhysicalDevice physicalDevice,
                              const ShadowMapConfig& config,
                              uint32_t framesInFlight) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    config_ = config;

    LOG_INFO("ResourcesShadow", "Creating shadow map resources (%ux%u)", 
             config_.resolution, config_.resolution);

    createShadowMap();
    createSampler();
    createRenderPass();
    createFramebuffer();
    createUBOBuffers(framesInFlight);

    // Initialize UBO with defaults
    shadowUBO_.lightSpaceMatrix = glm::mat4(1.0f);
    shadowUBO_.lightPosition = glm::vec4(0.0f, 10.0f, 0.0f, 0.0f);
    shadowUBO_.shadowMinBias = config_.minBias;
    shadowUBO_.shadowMaxBias = config_.maxBias;
    shadowUBO_.shadowNearPlane = config_.nearPlane;
    shadowUBO_.shadowFarPlane = config_.farPlane;

    LOG_INFO("ResourcesShadow", "Shadow map resources created successfully");
}

void ResourcesShadow::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    // Destroy UBO buffers
    for (auto& ubo : uboBuffers_) {
        if (ubo.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, ubo.buffer, nullptr);
        }
        if (ubo.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, ubo.memory, nullptr);
        }
    }
    uboBuffers_.clear();

    // Destroy framebuffer and render pass
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    // Destroy sampler
    if (shadowSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, shadowSampler_, nullptr);
        shadowSampler_ = VK_NULL_HANDLE;
    }

    // Destroy shadow map
    if (shadowMap_.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, shadowMap_.view, nullptr);
        shadowMap_.view = VK_NULL_HANDLE;
    }

    if (shadowMap_.image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, shadowMap_.image, nullptr);
        shadowMap_.image = VK_NULL_HANDLE;
    }

    if (shadowMap_.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, shadowMap_.memory, nullptr);
        shadowMap_.memory = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
}

void ResourcesShadow::createShadowMap() {
    // Create depth image for shadow map
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = config_.resolution;
    imageInfo.extent.height = config_.resolution;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &shadowMap_.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map image");
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, shadowMap_.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice_, memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &shadowMap_.memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow map memory");
    }

    vkBindImageMemory(device_, shadowMap_.image, shadowMap_.memory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowMap_.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &shadowMap_.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map image view");
    }

    shadowMap_.format = VK_FORMAT_D32_SFLOAT;
    shadowMap_.width = config_.resolution;
    shadowMap_.height = config_.resolution;
    shadowMap_.layers = 1;
    shadowMap_.mipLevels = 1;
}

void ResourcesShadow::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_TRUE;  // Enable depth comparison
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;  // Outside shadow = lit
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &shadowSampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map sampler");
    }
}

void ResourcesShadow::createRenderPass() {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Dependency for reading in subsequent passes
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map render pass");
    }
}

void ResourcesShadow::createFramebuffer() {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowMap_.view;
    framebufferInfo.width = config_.resolution;
    framebufferInfo.height = config_.resolution;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow map framebuffer");
    }
}

void ResourcesShadow::createUBOBuffers(uint32_t framesInFlight) {
    uboBuffers_.resize(framesInFlight);

    VkDeviceSize bufferSize = sizeof(ShadowMapUBO);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Create buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &uboBuffers_[i].buffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shadow UBO buffer");
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_, uboBuffers_[i].buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice_, memRequirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &uboBuffers_[i].memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate shadow UBO memory");
        }

        vkBindBufferMemory(device_, uboBuffers_[i].buffer, uboBuffers_[i].memory, 0);

        // Map memory persistently
        vkMapMemory(device_, uboBuffers_[i].memory, 0, bufferSize, 0, &uboBuffers_[i].mappedData);
        uboBuffers_[i].size = bufferSize;
    }
}

void ResourcesShadow::updateShadowUBO(uint32_t frameIndex,
                                       const glm::vec3& lightPos,
                                       const glm::vec3& lightDir,
                                       const glm::vec3& targetPos) {
    // Calculate light-space matrix for directional light
    // For a directional light, the view direction should be FIXED (lightDir),
    // not rotating to track the camera. We position the light above the scene
    // and look in the fixed light direction.
    glm::vec3 lookTarget = lightPos + glm::normalize(lightDir);

    // Choose an up vector that's not parallel to the light direction
    // If light is mostly vertical, use X as up instead of Y
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(glm::normalize(lightDir), up)) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::mat4 lightView = glm::lookAt(lightPos, lookTarget, up);

    // Use orthographic projection for directional lights
    // glm::orthoRH_ZO = Right-Handed, Zero-to-One depth range
    // RH matches lookAt's view space (negative Z for objects in front)
    float orthoSize = config_.orthoSize;
    glm::mat4 lightProjection = glm::orthoRH_ZO(
        -orthoSize, orthoSize,
        -orthoSize, orthoSize,
        config_.nearPlane, config_.farPlane
    );

    // Vulkan Y flip (Y points down in NDC)
    lightProjection[1][1] *= -1.0f;

    shadowUBO_.lightSpaceMatrix = lightProjection * lightView;
    shadowUBO_.lightPosition = glm::vec4(lightPos, 0.0f);  // w=0 for directional
    shadowUBO_.shadowMinBias = config_.minBias;
    shadowUBO_.shadowMaxBias = config_.maxBias;
    shadowUBO_.shadowNearPlane = config_.nearPlane;
    shadowUBO_.shadowFarPlane = config_.farPlane;

    // Upload to GPU
    std::memcpy(uboBuffers_[frameIndex].mappedData, &shadowUBO_, sizeof(ShadowMapUBO));
}

} // namespace jupiter::rendering

