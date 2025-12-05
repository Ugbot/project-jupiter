#pragma once

#include "rendering/texture.h"
#include <memory>

namespace jupiter {
namespace rendering {

/**
 * @brief Singleton providing default placeholder textures for missing material slots
 *
 * Following reference implementations (vulkan-gltf-pbr, hellovulkan), we provide
 * 1x1 pixel fallback textures to ensure all descriptor bindings are valid even
 * when materials are missing textures.
 *
 * Textures provided:
 * - White: Default albedo (opaque white)
 * - Normal: Flat normal map (128,128,255 = pointing "up" in tangent space)
 * - Black: Default metallic-roughness/emissive (no metal, smooth, no emission)
 */
class DefaultTextures {
public:
    // Singleton access
    static DefaultTextures& get();

    // Initialize default textures (call once during engine startup)
    bool initialize(VkDevice device, VmaAllocator allocator,
                   VkCommandPool commandPool, VkQueue graphicsQueue);

    // Cleanup (call during engine shutdown)
    void destroy();

    // Get placeholder textures
    VulkanTexture* getWhiteTexture() { return whiteTexture_.get(); }
    VulkanTexture* getNormalTexture() { return normalTexture_.get(); }
    VulkanTexture* getBlackTexture() { return blackTexture_.get(); }

    // Check if initialized
    bool isInitialized() const { return initialized_; }

private:
    DefaultTextures() = default;
    ~DefaultTextures() { destroy(); }

    // Non-copyable
    DefaultTextures(const DefaultTextures&) = delete;
    DefaultTextures& operator=(const DefaultTextures&) = delete;

    std::unique_ptr<VulkanTexture> whiteTexture_;
    std::unique_ptr<VulkanTexture> normalTexture_;
    std::unique_ptr<VulkanTexture> blackTexture_;
    bool initialized_ = false;
};

} // namespace rendering
} // namespace jupiter
