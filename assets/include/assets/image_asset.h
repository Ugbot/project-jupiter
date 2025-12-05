#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace jupiter {
namespace assets {

/**
 * @brief Image format enum (platform-agnostic)
 *
 * Describes the pixel format of image data.
 * Maps to Vulkan formats in rendering module, but defined here
 * to be independent of Vulkan.
 */
enum class ImageFormat : uint32_t {
    UNKNOWN = 0,
    R8 = 1,          // 8-bit single channel
    RGB8 = 2,        // 8-bit RGB (24 bits per pixel)
    RGBA8 = 3,       // 8-bit RGBA (32 bits per pixel)
    RGBA8_SRGB = 4,  // 8-bit RGBA in sRGB color space
    R16F = 5,        // 16-bit float single channel
    RGBA16F = 6,     // 16-bit float RGBA (HDR)
    R32F = 7,        // 32-bit float single channel
    RGBA32F = 8,     // 32-bit float RGBA (HDR)
};

/**
 * @brief Get bytes per pixel for a format
 */
inline uint32_t getImageFormatBytesPerPixel(ImageFormat format) {
    switch (format) {
        case ImageFormat::R8:        return 1;
        case ImageFormat::RGB8:      return 3;
        case ImageFormat::RGBA8:     return 4;
        case ImageFormat::RGBA8_SRGB:return 4;
        case ImageFormat::R16F:      return 2;
        case ImageFormat::RGBA16F:   return 8;
        case ImageFormat::R32F:      return 4;
        case ImageFormat::RGBA32F:   return 16;
        default: return 0;
    }
}

/**
 * @brief Raw image data (CPU-only, no VkImage)
 *
 * Contains decoded pixel data as a byte array.
 * No Vulkan types - can be loaded without GPU context.
 *
 * Following CLAUDE.md:
 * - Pre-allocated during load (no runtime allocations)
 * - POD layout where possible
 * - Platform-agnostic
 *
 * Useful for:
 * - Server-side asset loading
 * - Asset baking pipelines
 * - Background thread loading
 * - CPU-side image processing
 */
struct ImageAsset {
    std::string uuid;           // Unique identifier
    std::string name;           // Human-readable name
    std::string sourceFilePath; // Original file path (for hot-reload)

    // Raw pixel data (pre-allocated during load)
    std::vector<uint8_t> pixelData;
    uint32_t width;             // Width in pixels
    uint32_t height;            // Height in pixels
    ImageFormat format;         // Pixel format
    uint32_t bytesPerPixel;     // Computed from format

    /**
     * @brief Mipmap level (optional, can be pre-generated on CPU)
     *
     * Mipmaps can be generated during asset loading or at runtime on GPU.
     * Pre-generating on CPU allows background processing.
     */
    struct MipLevel {
        std::vector<uint8_t> data;
        uint32_t width;
        uint32_t height;

        MipLevel() : width(0), height(0) {}
        MipLevel(uint32_t w, uint32_t h) : width(w), height(h) {
            data.reserve(w * h * 4);  // Assume RGBA for reserve
        }
    };

    std::vector<MipLevel> mipLevels;  // Optional pre-generated mipmaps

    /**
     * @brief Default constructor
     */
    ImageAsset()
        : width(0)
        , height(0)
        , format(ImageFormat::RGBA8)
        , bytesPerPixel(4) {}

    /**
     * @brief Calculate total data size for base level
     */
    size_t getDataSize() const {
        return width * height * bytesPerPixel;
    }

    /**
     * @brief Check if mipmaps are present
     */
    bool hasMipmaps() const {
        return !mipLevels.empty();
    }
};

} // namespace assets
} // namespace jupiter
