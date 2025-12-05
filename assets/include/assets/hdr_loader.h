#pragma once

#include "assets/image_asset.h"
#include <string>

namespace jupiter {
namespace assets {

/**
 * @brief HDR image loader using stb_image
 *
 * Loads HDR images (.hdr format) with floating-point precision.
 * Useful for environment maps and high dynamic range textures.
 */
class HDRLoader {
public:
    /**
     * @brief Load HDR image from file
     * @param filepath Path to .hdr file
     * @param outAsset Output image asset (RGBA32F format)
     * @param flipVertically Whether to flip image vertically (for OpenGL/Vulkan convention)
     * @return true if successful
     */
    static bool loadFromFile(const std::string& filepath, ImageAsset& outAsset, bool flipVertically = false);

    /**
     * @brief Check if file is HDR format
     * @param filepath Path to image file
     * @return true if file is HDR (.hdr extension)
     */
    static bool isHDRFile(const std::string& filepath);
};

} // namespace assets
} // namespace jupiter
