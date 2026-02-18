#include "rendering/texture.h"
#include "assets/image_asset.h"
#include "logging/logging.h"

// stb_image for loading textures
// NOTE: STB_IMAGE_IMPLEMENTATION is defined in assets/src/gltf_loader.cpp
// to avoid duplicate symbols with TinyGLTF
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace jupiter {
namespace rendering {

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
    : device_(other.device_)
    , allocator_(other.allocator_)
    , image_(other.image_)
    , allocation_(other.allocation_)
    , imageView_(other.imageView_)
    , sampler_(other.sampler_)
    , width_(other.width_)
    , height_(other.height_)
    , mipLevels_(other.mipLevels_)
    , layerCount_(other.layerCount_)
    , format_(other.format_)
    , isCubemap_(other.isCubemap_) {
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = VK_NULL_HANDLE;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = VK_NULL_HANDLE;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        allocator_ = other.allocator_;
        image_ = other.image_;
        allocation_ = other.allocation_;
        imageView_ = other.imageView_;
        sampler_ = other.sampler_;
        width_ = other.width_;
        height_ = other.height_;
        mipLevels_ = other.mipLevels_;
        layerCount_ = other.layerCount_;
        format_ = other.format_;
        isCubemap_ = other.isCubemap_;

        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.imageView_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

void VulkanTexture::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
            sampler_ = VK_NULL_HANDLE;
        }
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, image_, allocation_);
            image_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
        }
    }
}

bool VulkanTexture::createFromFile(VkDevice device, VmaAllocator allocator,
                                   VkCommandPool commandPool, VkQueue graphicsQueue,
                                   const std::string& filepath, bool generateMipmaps) {
    // Load image using stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        LOG_ERROR("Texture", "Failed to load texture: %s", filepath.c_str());
        return false;
    }

    LOG_INFO("Texture", "Loaded texture: %s (%dx%d, %d channels)",
             filepath.c_str(), texWidth, texHeight, texChannels);

    bool result = create(device, allocator, commandPool, graphicsQueue,
                        pixels, texWidth, texHeight,
                        VK_FORMAT_R8G8B8A8_SRGB, generateMipmaps);

    stbi_image_free(pixels);
    return result;
}

bool VulkanTexture::createFromAsset(VkDevice device, VmaAllocator allocator,
                                    VkCommandPool commandPool, VkQueue graphicsQueue,
                                    const assets::ImageAsset& imageAsset,
                                    bool generateMipmaps) {
    if (imageAsset.pixelData.empty()) {
        LOG_ERROR("Texture", "Cannot create texture from empty ImageAsset");
        return false;
    }

    LOG_INFO("Texture", "Creating texture from ImageAsset: %s (%dx%d)",
             imageAsset.name.c_str(), imageAsset.width, imageAsset.height);

    // Map ImageFormat to VkFormat
    // NOTE: Use UNORM for all 8-bit textures and convert sRGB->linear manually in shader
    // This matches the reference vulkan-gltf-pbr implementation
    VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM; // Default
    switch (imageAsset.format) {
        case assets::ImageFormat::R8:
            vkFormat = VK_FORMAT_R8_UNORM;
            break;
        case assets::ImageFormat::RGB8:
            vkFormat = VK_FORMAT_R8G8B8_UNORM;
            break;
        case assets::ImageFormat::RGBA8:
            vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case assets::ImageFormat::RGBA8_SRGB:
            // Use UNORM - shader does manual pow(2.2) conversion like HelloVulkan
            // This is more reliable than relying on Vulkan's auto-conversion
            vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        case assets::ImageFormat::R16F:
            vkFormat = VK_FORMAT_R16_SFLOAT;
            break;
        case assets::ImageFormat::RGBA16F:
            vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            break;
        case assets::ImageFormat::R32F:
            vkFormat = VK_FORMAT_R32_SFLOAT;
            break;
        case assets::ImageFormat::RGBA32F:
            vkFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
            break;
        default:
            LOG_WARN("Texture", "Unknown ImageFormat, using RGBA8_SRGB");
            break;
    }

    return create(device, allocator, commandPool, graphicsQueue,
                 imageAsset.pixelData.data(), imageAsset.width, imageAsset.height,
                 vkFormat, generateMipmaps);
}

bool VulkanTexture::create(VkDevice device, VmaAllocator allocator,
                           VkCommandPool commandPool, VkQueue graphicsQueue,
                           const void* pixels, uint32_t width, uint32_t height,
                           VkFormat format, bool generateMipmaps) {
    device_ = device;
    allocator_ = allocator;
    width_ = width;
    height_ = height;
    format_ = format;
    isCubemap_ = false;
    layerCount_ = 1;

    // Calculate mip levels
    if (generateMipmaps) {
        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    } else {
        mipLevels_ = 1;
    }

    // Calculate bytes per pixel based on format
    uint32_t bytesPerPixel = 4; // Default for RGBA8
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            bytesPerPixel = 4;
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            bytesPerPixel = 8;
            break;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            bytesPerPixel = 16;
            break;
        default:
            LOG_INFO("Texture", "Unknown format, assuming 4 bytes per pixel");
            bytesPerPixel = 4;
            break;
    }
    
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mappedInfo;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer,
                       &stagingAllocation, &mappedInfo) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create staging buffer");
        return false;
    }

    // Copy pixel data to staging buffer
    memcpy(mappedInfo.pMappedData, pixels, static_cast<size_t>(imageSize));

    // Create image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (generateMipmaps) {
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // Needed for blit operations
    }

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &image_,
                      &allocation_, nullptr) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create image");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    // Execute transfer commands
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device_, commandPool);

    // Transition to transfer dst layout
    transitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer to image
    copyBufferToImage(commandBuffer, stagingBuffer);

    if (generateMipmaps) {
        // Generate mipmaps (also transitions to shader read layout)
        if (!this->generateMipmaps(commandBuffer)) {
            LOG_ERROR("Texture", "Failed to generate mipmaps");
            endSingleTimeCommands(device_, commandPool, graphicsQueue, commandBuffer);
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }
    } else {
        // Transition to shader read layout
        transitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    endSingleTimeCommands(device_, commandPool, graphicsQueue, commandBuffer);

    // Cleanup staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView_) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create image view");
        return false;
    }

    LOG_INFO("Texture", "Created texture (%dx%d) with %d mip levels", width, height, mipLevels_);
    return true;
}

bool VulkanTexture::createSampler(VkDevice device, VkFilter filter,
                                  VkSamplerAddressMode addressMode,
                                  bool anisotropyEnable, float maxAnisotropy) {
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = anisotropyEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = anisotropyEnable ? maxAnisotropy : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels_);

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create sampler");
        return false;
    }

    LOG_INFO("Texture", "Created sampler (filter=%d, addressMode=%d, anisotropy=%s)",
             filter, addressMode, anisotropyEnable ? "enabled" : "disabled");
    return true;
}

void VulkanTexture::transitionImageLayout(VkCommandBuffer commandBuffer,
                                         VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels_;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount_;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        LOG_ERROR("Texture", "Unsupported layout transition");
        return;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanTexture::copyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer) {
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width_, height_, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image_,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

bool VulkanTexture::generateMipmaps(VkCommandBuffer commandBuffer) {
    int32_t mipWidth = width_;
    int32_t mipHeight = height_;

    for (uint32_t i = 1; i < mipLevels_; i++) {
        // Transition previous mip level to transfer src
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image_;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                            0, nullptr, 0, nullptr, 1, &barrier);

        // Blit from previous level to current level
        VkImageBlit blit = {};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
                              mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
                      image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1, &blit, VK_FILTER_LINEAR);

        // Transition previous level to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                            0, nullptr, 0, nullptr, 1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last mip level to shader read
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevels_ - 1;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);

    return true;
}

VkCommandBuffer VulkanTexture::beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanTexture::endSingleTimeCommands(VkDevice device, VkCommandPool commandPool,
                                         VkQueue graphicsQueue, VkCommandBuffer commandBuffer) {
    LOG_INFO("Texture", "Ending command buffer...");
    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Texture", "vkEndCommandBuffer failed: %d", result);
        return;
    }
    LOG_INFO("Texture", "Command buffer ended");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    LOG_INFO("Texture", "Submitting to queue (queue=%p, submitInfo.commandBufferCount=%u)...",
             (void*)graphicsQueue, submitInfo.commandBufferCount);
    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Texture", "vkQueueSubmit failed: %d", result);
        return;
    }
    LOG_INFO("Texture", "Queue submitted, waiting for idle...");

    result = vkQueueWaitIdle(graphicsQueue);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Texture", "vkQueueWaitIdle failed: %d", result);
        return;
    }
    LOG_INFO("Texture", "Queue idle");

    LOG_INFO("Texture", "Freeing command buffer...");
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    LOG_INFO("Texture", "Command buffer freed");
}

bool VulkanTexture::createCubemap(VkDevice device, VmaAllocator allocator,
                                  VkCommandPool commandPool, VkQueue graphicsQueue,
                                  const void* const facePixels[6], uint32_t faceSize,
                                  VkFormat format, bool generateMipmaps) {
    device_ = device;
    allocator_ = allocator;
    width_ = faceSize;
    height_ = faceSize;
    format_ = format;
    isCubemap_ = true;
    layerCount_ = 6;

    // Calculate mip levels
    if (generateMipmaps) {
        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(faceSize))) + 1;
    } else {
        mipLevels_ = 1;
    }

    // Calculate bytes per pixel based on format
    uint32_t bytesPerPixel = 16; // Default for RGBA32F
    if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB) {
        bytesPerPixel = 4;
    } else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        bytesPerPixel = 8;
    }

    VkDeviceSize layerSize = faceSize * faceSize * bytesPerPixel;
    VkDeviceSize imageSize = layerSize * 6;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mappedInfo;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer,
                       &stagingAllocation, &mappedInfo) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create staging buffer for cubemap");
        return false;
    }

    // Copy all 6 faces to staging buffer
    uint8_t* data = static_cast<uint8_t*>(mappedInfo.pMappedData);
    for (uint32_t i = 0; i < 6; i++) {
        memcpy(data + (i * layerSize), facePixels[i], static_cast<size_t>(layerSize));
    }

    // Create cubemap image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = faceSize;
    imageInfo.extent.height = faceSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (generateMipmaps) {
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &image_,
                      &allocation_, nullptr) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create cubemap image");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    // Execute transfer commands
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(device_, commandPool);

    // Transition to transfer dst layout
    transitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer to all 6 faces
    std::vector<VkBufferImageCopy> regions(6);
    for (uint32_t face = 0; face < 6; face++) {
        regions[face].bufferOffset = layerSize * face;
        regions[face].bufferRowLength = 0;
        regions[face].bufferImageHeight = 0;
        regions[face].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        regions[face].imageSubresource.mipLevel = 0;
        regions[face].imageSubresource.baseArrayLayer = face;
        regions[face].imageSubresource.layerCount = 1;
        regions[face].imageOffset = {0, 0, 0};
        regions[face].imageExtent = {faceSize, faceSize, 1};
    }

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image_,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          static_cast<uint32_t>(regions.size()), regions.data());

    // Transition to shader read layout
    transitionImageLayout(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    endSingleTimeCommands(device_, commandPool, graphicsQueue, commandBuffer);

    // Cleanup staging buffer
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    // Create cubemap image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView_) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create cubemap image view");
        return false;
    }

    LOG_INFO("Texture", "Created cubemap (%dx%d) with %d mip levels", faceSize, faceSize, mipLevels_);
    return true;
}

bool VulkanTexture::createCubemapEmpty(VkDevice device, VmaAllocator allocator,
                                       uint32_t faceSize, VkFormat format,
                                       bool generateMipmaps, VkImageUsageFlags usage) {
    device_ = device;
    allocator_ = allocator;
    width_ = faceSize;
    height_ = faceSize;
    format_ = format;
    isCubemap_ = true;
    layerCount_ = 6;

    // Calculate mip levels
    if (generateMipmaps) {
        mipLevels_ = static_cast<uint32_t>(std::floor(std::log2(faceSize))) + 1;
    } else {
        mipLevels_ = 1;
    }

    // Create cubemap image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = faceSize;
    imageInfo.extent.height = faceSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &image_,
                      &allocation_, nullptr) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create empty cubemap image");
        return false;
    }

    // Create cubemap image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView_) != VK_SUCCESS) {
        LOG_ERROR("Texture", "Failed to create empty cubemap image view");
        return false;
    }

    LOG_INFO("Texture", "Created empty cubemap (%dx%d) with %d mip levels", faceSize, faceSize, mipLevels_);
    return true;
}

void VulkanTexture::setImageData(VkDevice device, VmaAllocator allocator,
                                 VkImage image, VkImageView imageView, VmaAllocation allocation,
                                 uint32_t width, uint32_t height, uint32_t mipLevels, uint32_t layerCount,
                                 VkFormat format) {
    // Destroy any existing resources
    destroy();

    // Store all provided handles and metadata
    device_ = device;
    allocator_ = allocator;
    image_ = image;
    imageView_ = imageView;
    allocation_ = allocation;
    width_ = width;
    height_ = height;
    mipLevels_ = mipLevels;
    layerCount_ = layerCount;
    format_ = format;
    isCubemap_ = (layerCount == 6);

    LOG_INFO("Texture", "Set image data directly (%dx%d, %d mips, %d layers, format=%d)",
             width, height, mipLevels, layerCount, format);
}

} // namespace rendering
} // namespace jupiter
