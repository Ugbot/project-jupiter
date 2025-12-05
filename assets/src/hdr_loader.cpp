#include "assets/hdr_loader.h"
#include "logging/logging.h"

// stb_image for HDR loading
// Note: STB_IMAGE_IMPLEMENTATION is defined in gltf_loader.cpp
#include <stb_image.h>

#include <algorithm>

namespace jupiter {
namespace assets {

bool HDRLoader::loadFromFile(const std::string& filepath, ImageAsset& outAsset, bool flipVertically) {
    LOG_INFO("HDR", "Loading HDR image: %s", filepath.c_str());

    // Set flip flag if requested
    stbi_set_flip_vertically_on_load(flipVertically);

    // Load HDR image as float data
    int width, height, channels;
    float* pixels = stbi_loadf(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        LOG_ERROR("HDR", "Failed to load HDR image: %s (reason: %s)",
                 filepath.c_str(), stbi_failure_reason());
        return false;
    }

    // HDR images must be equirectangular (roughly 2:1 aspect ratio)
    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    if (aspectRatio < 1.8f || aspectRatio > 2.2f) {
        LOG_WARN("HDR", "HDR image %s has unusual aspect ratio %.2f (expected ~2:1 for equirectangular)",
                filepath.c_str(), aspectRatio);
    }

    // Populate image asset
    outAsset.uuid = ""; // Will be assigned by caller
    outAsset.name = filepath;
    outAsset.sourceFilePath = filepath;
    outAsset.width = static_cast<uint32_t>(width);
    outAsset.height = static_cast<uint32_t>(height);
    outAsset.format = ImageFormat::RGBA32F;
    outAsset.bytesPerPixel = 16; // 4 floats × 4 bytes = 16 bytes per pixel

    // Copy float data to byte array
    size_t dataSize = width * height * 4 * sizeof(float);
    outAsset.pixelData.resize(dataSize);
    memcpy(outAsset.pixelData.data(), pixels, dataSize);

    // Free stb_image data
    stbi_image_free(pixels);

    LOG_INFO("HDR", "Loaded HDR image: %dx%d, %d channels, %.2f MB",
             width, height, 4, static_cast<float>(dataSize) / (1024.0f * 1024.0f));

    // Reset flip flag to default
    stbi_set_flip_vertically_on_load(false);

    return true;
}

bool HDRLoader::isHDRFile(const std::string& filepath) {
    // Check file extension
    size_t dotPos = filepath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    std::string ext = filepath.substr(dotPos + 1);

    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return (ext == "hdr");
}

} // namespace assets
} // namespace jupiter
