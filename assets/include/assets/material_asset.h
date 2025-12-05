#pragma once

#include <string>
#include <cstdint>

namespace jupiter {
namespace assets {

/**
 * @brief PBR material properties (CPU-only, no descriptor sets)
 *
 * Follows GLTF 2.0 Metallic-Roughness PBR workflow.
 * Contains material parameters and references to texture assets by UUID.
 * No Vulkan types - can be used without GPU context.
 *
 * Following CLAUDE.md:
 * - POD structure
 * - No runtime allocations
 * - Platform-agnostic (no GPU dependencies)
 *
 * The rendering module will create GPU descriptors from this data.
 */
struct MaterialAsset {
    std::string uuid;  // Unique identifier
    std::string name;  // Human-readable name

    // PBR Metallic-Roughness factors
    float baseColorFactor[4];   // RGBA (default: 1,1,1,1)
    float metallicFactor;       // 0 = dielectric, 1 = metal (default: 1)
    float roughnessFactor;      // 0 = smooth, 1 = rough (default: 1)
    float emissiveFactor[3];    // RGB emissive color (default: 0,0,0)

    // Texture references (UUIDs of ImageAssets, empty = no texture)
    std::string baseColorTextureUuid;          // Albedo/diffuse color
    std::string normalTextureUuid;             // Normal map (tangent space)
    std::string metallicRoughnessTextureUuid;  // G=roughness, B=metallic
    std::string occlusionTextureUuid;          // Ambient occlusion (R channel)
    std::string emissiveTextureUuid;           // Emissive mask

    // Material properties
    enum class AlphaMode : uint32_t {
        OPAQUE = 0,  // No alpha blending
        MASK = 1,    // Alpha test with cutoff
        BLEND = 2    // Alpha blending
    };

    AlphaMode alphaMode;   // How to handle alpha channel
    float alphaCutoff;     // Alpha test threshold (for MASK mode)
    bool doubleSided;      // Disable backface culling

    /**
     * @brief Default constructor with sensible PBR defaults
     */
    MaterialAsset()
        : baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f}
        , metallicFactor(1.0f)
        , roughnessFactor(1.0f)
        , emissiveFactor{0.0f, 0.0f, 0.0f}
        , alphaMode(AlphaMode::OPAQUE)
        , alphaCutoff(0.5f)
        , doubleSided(false) {}
};

} // namespace assets
} // namespace jupiter
