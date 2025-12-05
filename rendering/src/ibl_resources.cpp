#include "rendering/ibl_resources.h"
#include "rendering/texture.h"
#include "rendering/vulkan_compute_pipeline.h"
#include "rendering/descriptor_builder.h"
#include "assets/hdr_loader.h"
#include "logging/logging.h"

#include <vector>

namespace jupiter {
namespace rendering {

bool IBLResources::generateFromHDR(VkDevice device, VmaAllocator allocator,
                                   VkCommandPool commandPool, VkQueue queue,
                                   const std::string& hdrFilepath) {
    LOG_INFO("IBL", "Generating IBL resources from HDR: %s", hdrFilepath.c_str());

    device_ = device;
    allocator_ = allocator;

    // TODO: Implement full IBL generation pipeline
    // For now, create fallback resources
    LOG_WARN("IBL", "Full HDR-based IBL generation not yet implemented, using fallback");
    return createFallback(device, allocator, commandPool, queue);
}

bool IBLResources::createFallback(VkDevice device, VmaAllocator allocator,
                                  VkCommandPool commandPool, VkQueue queue) {
    LOG_INFO("IBL", "Creating fallback IBL resources (neutral gray environment)");

    device_ = device;
    allocator_ = allocator;

    // Create neutral gray cubemaps (1x1 per face for simplicity)
    const uint32_t fallbackSize = 1;
    const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    // Neutral gray color (0.5, 0.5, 0.5, 1.0) in linear space
    float grayPixel[4] = {0.5f, 0.5f, 0.5f, 1.0f};

    // Create 6 identical gray faces
    const void* facePixels[6] = {
        grayPixel, grayPixel, grayPixel,
        grayPixel, grayPixel, grayPixel
    };

    // Create irradiance map
    irradianceMap_ = std::make_unique<VulkanTexture>();
    if (!irradianceMap_->createCubemap(device, allocator, commandPool, queue,
                                       facePixels, fallbackSize, format, false)) {
        LOG_ERROR("IBL", "Failed to create fallback irradiance map");
        return false;
    }

    // Create sampler for irradiance map
    if (!irradianceMap_->createSampler(device, VK_FILTER_LINEAR,
                                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                       false, 1.0f)) {
        LOG_ERROR("IBL", "Failed to create irradiance map sampler");
        return false;
    }

    // Create prefiltered map (with mipmaps for different roughness levels)
    prefilteredMap_ = std::make_unique<VulkanTexture>();
    if (!prefilteredMap_->createCubemap(device, allocator, commandPool, queue,
                                        facePixels, fallbackSize, format, false)) {
        LOG_ERROR("IBL", "Failed to create fallback prefiltered map");
        return false;
    }

    // Create sampler for prefiltered map
    if (!prefilteredMap_->createSampler(device, VK_FILTER_LINEAR,
                                        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                        false, 1.0f)) {
        LOG_ERROR("IBL", "Failed to create prefiltered map sampler");
        return false;
    }

    // Create BRDF LUT (simple fallback: constant 1.0, 0.0)
    brdfLUT_ = std::make_unique<VulkanTexture>();
    const uint32_t lutSize = 1;
    float lutPixel[2] = {1.0f, 0.0f}; // Scale=1.0, Bias=0.0

    if (!brdfLUT_->create(device, allocator, commandPool, queue,
                         lutPixel, lutSize, lutSize,
                         VK_FORMAT_R32G32_SFLOAT, false)) {
        LOG_ERROR("IBL", "Failed to create fallback BRDF LUT");
        return false;
    }

    // Create sampler for BRDF LUT
    if (!brdfLUT_->createSampler(device, VK_FILTER_LINEAR,
                                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                false, 1.0f)) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT sampler");
        return false;
    }

    LOG_INFO("IBL", "Fallback IBL resources created successfully");
    return true;
}

void IBLResources::destroy() {
    irradianceMap_.reset();
    prefilteredMap_.reset();
    brdfLUT_.reset();
    environmentMap_.reset();

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

bool IBLResources::isValid() const {
    return irradianceMap_ && irradianceMap_->isValid() &&
           prefilteredMap_ && prefilteredMap_->isValid() &&
           brdfLUT_ && brdfLUT_->isValid();
}

VkImageView IBLResources::getIrradianceMapView() const {
    return irradianceMap_ ? irradianceMap_->getImageView() : VK_NULL_HANDLE;
}

VkImageView IBLResources::getPrefilteredMapView() const {
    return prefilteredMap_ ? prefilteredMap_->getImageView() : VK_NULL_HANDLE;
}

VkImageView IBLResources::getBRDFLUTView() const {
    return brdfLUT_ ? brdfLUT_->getImageView() : VK_NULL_HANDLE;
}

VkSampler IBLResources::getIrradianceMapSampler() const {
    return irradianceMap_ ? irradianceMap_->getSampler() : VK_NULL_HANDLE;
}

VkSampler IBLResources::getPrefilteredMapSampler() const {
    return prefilteredMap_ ? prefilteredMap_->getSampler() : VK_NULL_HANDLE;
}

VkSampler IBLResources::getBRDFLUTSampler() const {
    return brdfLUT_ ? brdfLUT_->getSampler() : VK_NULL_HANDLE;
}

uint32_t IBLResources::getMaxReflectionLod() const {
    return prefilteredMap_ ? (prefilteredMap_->getMipLevels() - 1) : 0;
}

// TODO: Implement generation methods
bool IBLResources::generateBRDFLUT(VkDevice device, VmaAllocator allocator,
                                   VkCommandPool commandPool, VkQueue queue) {
    LOG_INFO("IBL", "Generating BRDF LUT (256x256, RG16F)...");

    device_ = device;
    allocator_ = allocator;

    const uint32_t LUT_SIZE = 256;
    const VkFormat LUT_FORMAT = VK_FORMAT_R16G16_SFLOAT;

    // Create empty 2D texture for BRDF LUT with STORAGE usage
    brdfLUT_ = std::make_unique<VulkanTexture>();

    // Create image with STORAGE usage for compute shader output
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = LUT_FORMAT;
    imageInfo.extent.width = LUT_SIZE;
    imageInfo.extent.height = LUT_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = LUT_FORMAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT image view");
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }

    // Transition image to GENERAL layout for compute shader
    VkCommandBuffer cmd = brdfLUT_->beginSingleTimeCommands(device_, commandPool);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    brdfLUT_->endSingleTimeCommands(device_, commandPool, queue, cmd);

    // Create descriptor set layout for storage image
    DescriptorSetLayoutBuilder layoutBuilder(device_);
    VkDescriptorSetLayout descriptorSetLayout = layoutBuilder
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1)
        .build();
    LOG_INFO("IBL", "Created descriptor set layout: %p", (void*)descriptorSetLayout);

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        LOG_ERROR("IBL", "Failed to create descriptor set layout!");
        vkDestroyImageView(device_, imageView, nullptr);
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }

    // Create descriptor pool
    DescriptorPoolBuilder poolBuilder(device_);
    VkDescriptorPool descriptorPool = poolBuilder
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1)
        .setMaxSets(1)
        .build();
    LOG_INFO("IBL", "Created descriptor pool: %p", (void*)descriptorPool);

    if (descriptorPool == VK_NULL_HANDLE) {
        LOG_ERROR("IBL", "Failed to create descriptor pool!");
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
        vkDestroyImageView(device_, imageView, nullptr);
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }

    // Allocate descriptor set
    DescriptorSetAllocator descAllocator(device_, descriptorPool);
    VkDescriptorSet descriptorSet = descAllocator.allocate(descriptorSetLayout);

    if (descriptorSet == VK_NULL_HANDLE) {
        LOG_ERROR("IBL", "Failed to allocate descriptor set!");
        vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
        vkDestroyImageView(device_, imageView, nullptr);
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }
    LOG_INFO("IBL", "Descriptor set allocated: %p", (void*)descriptorSet);

    // Update descriptor set with BRDF LUT image
    VkDescriptorImageInfo imageDescInfo = {};
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageDescInfo.imageView = imageView;
    imageDescInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageDescInfo;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    // Create compute pipeline
    vulkan::VulkanComputePipeline computePipeline;
    std::vector<VkDescriptorSetLayout> layouts = { descriptorSetLayout };

    if (!computePipeline.create(device_, "ibl/brdf_lut.comp.spv", layouts, nullptr)) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT compute pipeline");
        vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
        vkDestroyImageView(device_, imageView, nullptr);
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }

    LOG_INFO("IBL", "Starting compute shader execution...");
    // Execute compute shader
    cmd = brdfLUT_->beginSingleTimeCommands(device_, commandPool);
    LOG_INFO("IBL", "Command buffer allocated: %p", (void*)cmd);

    if (cmd == VK_NULL_HANDLE) {
        LOG_ERROR("IBL", "Failed to allocate command buffer!");
        return false;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.getPipeline());
    LOG_INFO("IBL", "Pipeline bound (pipeline=%p)", (void*)computePipeline.getPipeline());

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.getLayout(),
                           0, 1, &descriptorSet, 0, nullptr);
    LOG_INFO("IBL", "Descriptor sets bound (layout=%p, set=%p)",
             (void*)computePipeline.getLayout(), (void*)descriptorSet);

    // Dispatch: one workgroup per pixel (local_size = 1,1,1)
    computePipeline.dispatch(cmd, LUT_SIZE, LUT_SIZE, 1);
    LOG_INFO("IBL", "Dispatch completed (256x256x1 workgroups)");

    // Barrier: compute write -> fragment read
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    LOG_INFO("IBL", "Executing pipeline barrier (image=%p)...", (void*)barrier.image);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    LOG_INFO("IBL", "Pipeline barrier completed");

    LOG_INFO("IBL", "Ending command buffer (device=%p, commandPool=%p, queue=%p, cmd=%p)...",
             (void*)device_, (void*)commandPool, (void*)queue, (void*)cmd);
    brdfLUT_->endSingleTimeCommands(device_, commandPool, queue, cmd);
    LOG_INFO("IBL", "Command buffer submitted");

    // Store in VulkanTexture (bypass normal create flow)
    LOG_INFO("IBL", "Setting image data...");
    brdfLUT_->setImageData(device_, allocator_, image, imageView, allocation,
                          LUT_SIZE, LUT_SIZE, 1, 1, LUT_FORMAT);
    LOG_INFO("IBL", "Image data set");

    // Create sampler
    LOG_INFO("IBL", "Creating sampler...");
    if (!brdfLUT_->createSampler(device_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT sampler");
        return false;
    }
    LOG_INFO("IBL", "Sampler created");

    // Cleanup temporary resources
    computePipeline.destroy();
    vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);

    LOG_INFO("IBL", "BRDF LUT generated successfully");
    return true;
}

bool IBLResources::convertEquirectToCubemap(VkCommandPool commandPool, VkQueue queue,
                                           VulkanTexture& hdrTexture) {
    LOG_WARN("IBL", "convertEquirectToCubemap() not yet implemented");
    return false;
}

bool IBLResources::filterDiffuseIrradiance(VkCommandPool commandPool, VkQueue queue) {
    LOG_WARN("IBL", "filterDiffuseIrradiance() not yet implemented");
    return false;
}

bool IBLResources::filterSpecularPrefiltered(VkCommandPool commandPool, VkQueue queue) {
    LOG_WARN("IBL", "filterSpecularPrefiltered() not yet implemented");
    return false;
}

} // namespace rendering
} // namespace jupiter
