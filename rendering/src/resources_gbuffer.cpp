/**
 * @file resources_gbuffer.cpp
 * @brief G-buffer resources implementation
 */

#include "rendering/resources_gbuffer.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cstring>
#include <stdexcept>
#include <array>

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

void createImage(VkDevice device, VkPhysicalDevice physicalDevice,
                 uint32_t width, uint32_t height, VkFormat format,
                 VkImageUsageFlags usage, GPUImage& outImage) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &outImage.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, outImage.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &outImage.memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory");
    }

    vkBindImageMemory(device, outImage.image, outImage.memory, 0);

    outImage.format = format;
    outImage.width = width;
    outImage.height = height;
    outImage.layers = 1;
    outImage.mipLevels = 1;
}

void createImageView(VkDevice device, GPUImage& image, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &image.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }
}

void destroyImage(VkDevice device, GPUImage& image) {
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image.view, nullptr);
        image.view = VK_NULL_HANDLE;
    }
    if (image.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, image.image, nullptr);
        image.image = VK_NULL_HANDLE;
    }
    if (image.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, image.memory, nullptr);
        image.memory = VK_NULL_HANDLE;
    }
}

} // anonymous namespace

ResourcesGBuffer::~ResourcesGBuffer() {
    destroy();
}

void ResourcesGBuffer::create(VkDevice device,
                               VkPhysicalDevice physicalDevice,
                               const GBufferConfig& config,
                               uint32_t framesInFlight) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    config_ = config;

    LOG_INFO("ResourcesGBuffer", "Creating G-buffer resources (%ux%u)", config_.width, config_.height);

    createPositionTexture();
    createNormalTexture();
    createDepthTexture();
    createNoiseTexture();
    createSSAOTexture();
    createKernelBuffer();
    createSamplers();
    createGBufferRenderPass();
    createGBufferFramebuffer();
    createSSAORenderPass();
    createSSAOFramebuffer();

    LOG_INFO("ResourcesGBuffer", "G-buffer resources created successfully");
}

void ResourcesGBuffer::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    // Destroy framebuffers
    if (gBufferFramebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, gBufferFramebuffer_, nullptr);
        gBufferFramebuffer_ = VK_NULL_HANDLE;
    }
    if (ssaoFramebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, ssaoFramebuffer_, nullptr);
        ssaoFramebuffer_ = VK_NULL_HANDLE;
    }

    // Destroy render passes
    if (gBufferRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, gBufferRenderPass_, nullptr);
        gBufferRenderPass_ = VK_NULL_HANDLE;
    }
    if (ssaoRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, ssaoRenderPass_, nullptr);
        ssaoRenderPass_ = VK_NULL_HANDLE;
    }

    // Destroy samplers
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (noiseSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, noiseSampler_, nullptr);
        noiseSampler_ = VK_NULL_HANDLE;
    }

    // Destroy kernel buffer
    if (kernel_.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, kernel_.buffer, nullptr);
        kernel_.buffer = VK_NULL_HANDLE;
    }
    if (kernel_.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, kernel_.memory, nullptr);
        kernel_.memory = VK_NULL_HANDLE;
    }

    // Destroy images
    destroyImage(device_, position_);
    destroyImage(device_, normal_);
    destroyImage(device_, depth_);
    destroyImage(device_, noise_);
    destroyImage(device_, ssao_);

    device_ = VK_NULL_HANDLE;
}

void ResourcesGBuffer::onWindowResized(uint32_t width, uint32_t height) {
    config_.width = width;
    config_.height = height;

    // Recreate resolution-dependent resources
    vkDestroyFramebuffer(device_, gBufferFramebuffer_, nullptr);
    vkDestroyFramebuffer(device_, ssaoFramebuffer_, nullptr);

    destroyImage(device_, position_);
    destroyImage(device_, normal_);
    destroyImage(device_, depth_);
    destroyImage(device_, ssao_);

    createPositionTexture();
    createNormalTexture();
    createDepthTexture();
    createSSAOTexture();
    createGBufferFramebuffer();
    createSSAOFramebuffer();
}

void ResourcesGBuffer::createPositionTexture() {
    // RGBA16F for view-space position + linear depth
    createImage(device_, physicalDevice_, config_.width, config_.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                position_);
    createImageView(device_, position_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void ResourcesGBuffer::createNormalTexture() {
    // RGBA16F for view-space normals
    createImage(device_, physicalDevice_, config_.width, config_.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                normal_);
    createImageView(device_, normal_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void ResourcesGBuffer::createDepthTexture() {
    createImage(device_, physicalDevice_, config_.width, config_.height,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                depth_);
    createImageView(device_, depth_, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void ResourcesGBuffer::createSSAOTexture() {
    // Single channel for SSAO output
    createImage(device_, physicalDevice_, config_.width, config_.height,
                VK_FORMAT_R8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                ssao_);
    createImageView(device_, ssao_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void ResourcesGBuffer::createNoiseTexture() {
    // Create a small noise texture (4x4) with random rotation vectors
    uint32_t noiseSize = config_.noiseSize;
    
    createImage(device_, physicalDevice_, noiseSize, noiseSize,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                noise_);
    createImageView(device_, noise_, VK_IMAGE_ASPECT_COLOR_BIT);

    // Note: Actual noise data upload happens in generateNoiseTexture()
    // which requires a command buffer - should be called during initialization
}

void ResourcesGBuffer::createKernelBuffer() {
    generateSSAOKernel();

    VkDeviceSize bufferSize = ssaoKernel_.size() * sizeof(glm::vec4);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &kernel_.buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO kernel buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, kernel_.buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice_, memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &kernel_.memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SSAO kernel memory");
    }

    vkBindBufferMemory(device_, kernel_.buffer, kernel_.memory, 0);

    // Upload kernel data
    void* data;
    vkMapMemory(device_, kernel_.memory, 0, bufferSize, 0, &data);
    std::memcpy(data, ssaoKernel_.data(), bufferSize);
    vkUnmapMemory(device_, kernel_.memory);

    kernel_.size = bufferSize;
}

void ResourcesGBuffer::createSamplers() {
    // Standard sampler for G-buffer textures
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer sampler");
    }

    // Repeating sampler for noise texture
    VkSamplerCreateInfo noiseSamplerInfo = samplerInfo;
    noiseSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    noiseSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    noiseSamplerInfo.magFilter = VK_FILTER_NEAREST;
    noiseSamplerInfo.minFilter = VK_FILTER_NEAREST;

    if (vkCreateSampler(device_, &noiseSamplerInfo, nullptr, &noiseSampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create noise sampler");
    }
}

void ResourcesGBuffer::createGBufferRenderPass() {
    std::array<VkAttachmentDescription, 3> attachments{};

    // Position attachment
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal attachment
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Depth attachment
    attachments[2].format = VK_FORMAT_D32_SFLOAT;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 2> colorAttachmentRefs{};
    colorAttachmentRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    colorAttachmentRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 2;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
    subpass.pColorAttachments = colorAttachmentRefs.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &gBufferRenderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer render pass");
    }
}

void ResourcesGBuffer::createGBufferFramebuffer() {
    std::array<VkImageView, 3> attachments = {
        position_.view,
        normal_.view,
        depth_.view
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = gBufferRenderPass_;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = config_.width;
    framebufferInfo.height = config_.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &gBufferFramebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer framebuffer");
    }
}

void ResourcesGBuffer::createSSAORenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &ssaoRenderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO render pass");
    }
}

void ResourcesGBuffer::createSSAOFramebuffer() {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = ssaoRenderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &ssao_.view;
    framebufferInfo.width = config_.width;
    framebufferInfo.height = config_.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &ssaoFramebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create SSAO framebuffer");
    }
}

void ResourcesGBuffer::generateSSAOKernel() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);

    ssaoKernel_.resize(config_.ssaoKernelSize);

    for (uint32_t i = 0; i < config_.ssaoKernelSize; ++i) {
        // Random point in hemisphere
        glm::vec3 sample(
            randomFloats(gen) * 2.0f - 1.0f,
            randomFloats(gen) * 2.0f - 1.0f,
            randomFloats(gen)  // Only positive Z (hemisphere)
        );
        sample = glm::normalize(sample);
        sample *= randomFloats(gen);

        // Scale samples to cluster near origin
        float scale = static_cast<float>(i) / static_cast<float>(config_.ssaoKernelSize);
        scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, scale^2)
        sample *= scale;

        ssaoKernel_[i] = glm::vec4(sample, 0.0f);
    }
}

void ResourcesGBuffer::generateNoiseTexture() {
    // Note: This requires a command buffer to upload texture data
    // Should be called during initialization with proper staging buffer
    // For now, the noise texture creation is handled by createNoiseTexture()
    // and actual data upload would need to happen separately
}

} // namespace jupiter::rendering

