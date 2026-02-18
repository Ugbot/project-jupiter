#include "rendering/ibl_resources.h"
#include "rendering/texture.h"
#include "rendering/vulkan_compute_pipeline.h"
#include "rendering/pipeline_ibl.h"
#include "rendering/descriptor_builder.h"
#include "assets/hdr_loader.h"
#include "logging/logging.h"
#include "profiling/profiler.h"

#include <vector>
#include <fstream>

namespace jupiter {
namespace rendering {

bool IBLResources::generateFromHDR(VkDevice device, VmaAllocator allocator,
                                   VkCommandPool commandPool, VkQueue queue,
                                   const std::string& hdrFilepath) {
    JUPITER_PROFILE_SCOPE("IBLResources::generateFromHDR");
    LOG_INFO("IBL", "Generating IBL resources from HDR: %s", hdrFilepath.c_str());

    device_ = device;
    allocator_ = allocator;

    // Build IBL pipeline once for this generation
    PipelineIBL iblPipeline;
    if (!iblPipeline.create(device, allocator, commandPool, queue)) {
        LOG_ERROR("IBL", "Failed to create IBL pipeline, falling back to default");
        return createFallback(device, allocator, commandPool, queue);
    }

    // Create environment cubemap from HDR
    auto envMap = std::make_unique<VulkanTexture>();
    if (!iblPipeline.convertEquirectToCubemap(hdrFilepath, envMap.get())) {
        LOG_ERROR("IBL", "Failed to convert equirect to cubemap, falling back to default");
        return createFallback(device, allocator, commandPool, queue);
    }

    // Store environment map for skybox and downstream filters
    environmentMap_ = std::move(envMap);

    // Generate diffuse irradiance map
    irradianceMap_ = std::make_unique<VulkanTexture>();
    if (!iblPipeline.filterDiffuseIrradiance(environmentMap_.get(), irradianceMap_.get())) {
        LOG_ERROR("IBL", "Failed to filter diffuse irradiance");
        return false;
    }

    // Generate specular prefiltered map
    prefilteredMap_ = std::make_unique<VulkanTexture>();
    if (!iblPipeline.filterSpecularPrefiltered(environmentMap_.get(), prefilteredMap_.get())) {
        LOG_ERROR("IBL", "Failed to filter specular prefiltered");
        return false;
    }

    // Generate BRDF LUT (compute)
    if (!generateBRDFLUT(device, allocator, commandPool, queue)) {
        LOG_ERROR("IBL", "Failed to generate BRDF LUT");
        return false;
    }

    LOG_INFO("IBL", "IBL resources generated successfully from HDR: %s", hdrFilepath.c_str());
    return true;
}

bool IBLResources::createFallback(VkDevice device, VmaAllocator allocator,
                                  VkCommandPool commandPool, VkQueue queue) {
    JUPITER_PROFILE_SCOPE("IBLResources::createFallback");
    LOG_INFO("IBL", "Creating fallback IBL resources (neutral gray environment)");

    device_ = device;
    allocator_ = allocator;

    // Create neutral gray cubemaps (1x1 per face for simplicity)
    const uint32_t fallbackSize = 1;
    const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;

    // Very dark gray to avoid washing out colors (0.05, 0.05, 0.05) in linear space
    // Real IBL should be loaded for proper environment lighting
    float grayPixel[4] = {0.08f, 0.08f, 0.08f, 1.0f};

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

VulkanTexture* IBLResources::getEnvironmentMap() const {
    return environmentMap_.get();
}

bool IBLResources::hasEnvironmentMap() const {
    return environmentMap_ && environmentMap_->isValid();
}

// Generate or load BRDF LUT
// Following industry best practice: BRDF LUT is view/environment independent,
// so generate once and cache to disk (see Ghost of Tsushima GDC talk)
bool IBLResources::generateBRDFLUT(VkDevice device, VmaAllocator allocator,
                                   VkCommandPool commandPool, VkQueue queue) {
    JUPITER_PROFILE_SCOPE("IBLResources::generateBRDFLUT");

    device_ = device;
    allocator_ = allocator;

    const uint32_t LUT_SIZE = 256;
    const VkFormat LUT_FORMAT = VK_FORMAT_R16G16_SFLOAT;
    const std::string cachePath = "brdf_lut.cache";

    // Try to load pre-baked BRDF LUT from cache first
    if (loadBRDFLUTFromCache(device, allocator, commandPool, queue, cachePath)) {
        LOG_INFO("IBL", "Loaded BRDF LUT from cache: %s", cachePath.c_str());
        return true;
    }

    LOG_INFO("IBL", "Generating BRDF LUT (256x256, RG16F) - this only happens once...");

    // Create new texture in local variable - only assign to brdfLUT_ on success
    // This preserves the fallback if generation fails
    auto newBrdfLUT = std::make_unique<VulkanTexture>();

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
    VkCommandBuffer cmd = newBrdfLUT->beginSingleTimeCommands(device_, commandPool);

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

    newBrdfLUT->endSingleTimeCommands(device_, commandPool, queue, cmd);

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

    if (!computePipeline.create(device_, "shaders/ibl/brdf_lut.comp.spv", layouts, nullptr)) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT compute pipeline");
        vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
        vkDestroyImageView(device_, imageView, nullptr);
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }

    LOG_INFO("IBL", "Starting compute shader execution...");
    // Execute compute shader
    cmd = newBrdfLUT->beginSingleTimeCommands(device_, commandPool);
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

    // Dispatch: workgroups cover 16x16 pixels each (local_size = 16,16,1)
    const uint32_t workgroupsX = (LUT_SIZE + 15) / 16;  // = 16
    const uint32_t workgroupsY = (LUT_SIZE + 15) / 16;  // = 16
    computePipeline.dispatch(cmd, workgroupsX, workgroupsY, 1);
    LOG_INFO("IBL", "Dispatch completed (%ux%ux1 workgroups)", workgroupsX, workgroupsY);

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
    newBrdfLUT->endSingleTimeCommands(device_, commandPool, queue, cmd);
    LOG_INFO("IBL", "Command buffer submitted");

    // Store in VulkanTexture (bypass normal create flow)
    LOG_INFO("IBL", "Setting image data...");
    newBrdfLUT->setImageData(device_, allocator_, image, imageView, allocation,
                          LUT_SIZE, LUT_SIZE, 1, 1, LUT_FORMAT);
    LOG_INFO("IBL", "Image data set");

    // Create sampler
    LOG_INFO("IBL", "Creating sampler...");
    if (!newBrdfLUT->createSampler(device_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("IBL", "Failed to create BRDF LUT sampler");
        return false;
    }
    LOG_INFO("IBL", "Sampler created");

    // Success! Replace the fallback with the compute-generated LUT
    brdfLUT_ = std::move(newBrdfLUT);

    // Save to cache for future runs (BRDF LUT is environment-independent)
    saveBRDFLUTToCache(device, commandPool, queue, cachePath, LUT_SIZE);

    // Cleanup temporary resources
    computePipeline.destroy();
    vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);

    LOG_INFO("IBL", "BRDF LUT generated and cached successfully");
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

bool IBLResources::loadBRDFLUTFromCache(VkDevice device, VmaAllocator allocator,
                                        VkCommandPool commandPool, VkQueue queue,
                                        const std::string& cachePath) {
    // Check if cache file exists
    std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_INFO("IBL", "No BRDF LUT cache found at: %s", cachePath.c_str());
        return false;
    }

    // Read header
    file.seekg(0);
    uint32_t magic, width, height;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&width), sizeof(width));
    file.read(reinterpret_cast<char*>(&height), sizeof(height));

    if (magic != 0x4C555442) { // "BTUL" reversed
        LOG_WARN("IBL", "Invalid BRDF LUT cache magic");
        return false;
    }

    if (width != 256 || height != 256) {
        LOG_WARN("IBL", "BRDF LUT cache size mismatch: %ux%u", width, height);
        return false;
    }

    // Read pixel data (RG16F = 4 bytes per pixel)
    const size_t dataSize = width * height * 4;
    std::vector<char> pixelData(dataSize);
    file.read(pixelData.data(), dataSize);

    if (!file) {
        LOG_ERROR("IBL", "Failed to read BRDF LUT cache data");
        return false;
    }
    file.close();

    // Create texture from cached data
    brdfLUT_ = std::make_unique<VulkanTexture>();
    if (!brdfLUT_->create(device, allocator, commandPool, queue,
                          pixelData.data(), width, height,
                          VK_FORMAT_R16G16_SFLOAT, false)) {
        LOG_ERROR("IBL", "Failed to create texture from BRDF LUT cache");
        brdfLUT_.reset();
        return false;
    }

    if (!brdfLUT_->createSampler(device, VK_FILTER_LINEAR,
                                 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("IBL", "Failed to create sampler for cached BRDF LUT");
        brdfLUT_.reset();
        return false;
    }

    return true;
}

void IBLResources::saveBRDFLUTToCache(VkDevice device, VkCommandPool commandPool,
                                      VkQueue queue, const std::string& cachePath,
                                      uint32_t size) {
    if (!brdfLUT_ || !brdfLUT_->isValid()) {
        LOG_WARN("IBL", "Cannot save BRDF LUT cache: texture not valid");
        return;
    }

    // Note: Reading back from GPU requires staging buffer + vkCmdCopyImageToBuffer
    // For now, we'll regenerate on each fresh install
    // A proper implementation would readback the GPU data here

    // Create staging buffer for readback
    const size_t dataSize = size * size * 4; // RG16F = 4 bytes/pixel

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        LOG_WARN("IBL", "Failed to create staging buffer for BRDF LUT cache");
        return;
    }

    // Copy image to staging buffer
    VkCommandBuffer cmd = brdfLUT_->beginSingleTimeCommands(device, commandPool);

    // Transition to transfer src
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = brdfLUT_->getImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {size, size, 1};

    vkCmdCopyImageToBuffer(cmd, brdfLUT_->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer, 1, &region);

    // Transition back to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    brdfLUT_->endSingleTimeCommands(device, commandPool, queue, cmd);

    // Map and read data
    void* mappedData;
    if (vmaMapMemory(allocator_, stagingAllocation, &mappedData) != VK_SUCCESS) {
        LOG_WARN("IBL", "Failed to map staging buffer for BRDF LUT cache");
        vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
        return;
    }

    // Write cache file
    std::ofstream file(cachePath, std::ios::binary);
    if (file) {
        uint32_t magic = 0x4C555442; // "BTUL"
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(mappedData), dataSize);
        LOG_INFO("IBL", "Saved BRDF LUT cache to: %s", cachePath.c_str());
    } else {
        LOG_WARN("IBL", "Failed to write BRDF LUT cache file");
    }

    vmaUnmapMemory(allocator_, stagingAllocation);
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
}

} // namespace rendering
} // namespace jupiter
