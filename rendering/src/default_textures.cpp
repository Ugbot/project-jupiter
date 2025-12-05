#include "rendering/default_textures.h"
#include "logging/logging.h"
#include <cstdint>

namespace jupiter {
namespace rendering {

DefaultTextures& DefaultTextures::get() {
    static DefaultTextures instance;
    return instance;
}

bool DefaultTextures::initialize(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool commandPool, VkQueue graphicsQueue) {
    if (initialized_) {
        LOG_WARN("DefaultTextures", "Already initialized");
        return true;
    }

    LOG_INFO("DefaultTextures", "Creating default placeholder textures...");

    // White texture (1x1) - for missing albedo/occlusion
    // RGBA = (255, 255, 255, 255) = opaque white
    uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    whiteTexture_ = std::make_unique<VulkanTexture>();
    if (!whiteTexture_->create(device, allocator, commandPool, graphicsQueue,
                               whitePixel, 1, 1,
                               VK_FORMAT_R8G8B8A8_SRGB, false)) {
        LOG_ERROR("DefaultTextures", "Failed to create white texture");
        return false;
    }
    if (!whiteTexture_->createSampler(device, VK_FILTER_NEAREST,
                                       VK_SAMPLER_ADDRESS_MODE_REPEAT, false)) {
        LOG_ERROR("DefaultTextures", "Failed to create white texture sampler");
        return false;
    }
    LOG_INFO("DefaultTextures", "Created white texture (1x1)");

    // Normal texture (1x1) - for missing normal maps
    // RGBA = (128, 128, 255, 255) = flat normal pointing "up" in tangent space
    // Note: Normal maps should NOT be sRGB - use UNORM format
    uint8_t normalPixel[4] = { 128, 128, 255, 255 };
    normalTexture_ = std::make_unique<VulkanTexture>();
    if (!normalTexture_->create(device, allocator, commandPool, graphicsQueue,
                                normalPixel, 1, 1,
                                VK_FORMAT_R8G8B8A8_UNORM, false)) {
        LOG_ERROR("DefaultTextures", "Failed to create normal texture");
        return false;
    }
    if (!normalTexture_->createSampler(device, VK_FILTER_NEAREST,
                                        VK_SAMPLER_ADDRESS_MODE_REPEAT, false)) {
        LOG_ERROR("DefaultTextures", "Failed to create normal texture sampler");
        return false;
    }
    LOG_INFO("DefaultTextures", "Created normal texture (1x1, flat)");

    // Black texture (1x1) - for missing metallic-roughness/emissive
    // RGBA = (0, 0, 0, 255) = no metal, smooth, no emission
    uint8_t blackPixel[4] = { 0, 0, 0, 255 };
    blackTexture_ = std::make_unique<VulkanTexture>();
    if (!blackTexture_->create(device, allocator, commandPool, graphicsQueue,
                               blackPixel, 1, 1,
                               VK_FORMAT_R8G8B8A8_UNORM, false)) {
        LOG_ERROR("DefaultTextures", "Failed to create black texture");
        return false;
    }
    if (!blackTexture_->createSampler(device, VK_FILTER_NEAREST,
                                       VK_SAMPLER_ADDRESS_MODE_REPEAT, false)) {
        LOG_ERROR("DefaultTextures", "Failed to create black texture sampler");
        return false;
    }
    LOG_INFO("DefaultTextures", "Created black texture (1x1)");

    initialized_ = true;
    LOG_INFO("DefaultTextures", "All default textures created successfully");
    return true;
}

void DefaultTextures::destroy() {
    if (!initialized_) return;

    LOG_INFO("DefaultTextures", "Destroying default textures...");
    whiteTexture_.reset();
    normalTexture_.reset();
    blackTexture_.reset();
    initialized_ = false;
}

} // namespace rendering
} // namespace jupiter
