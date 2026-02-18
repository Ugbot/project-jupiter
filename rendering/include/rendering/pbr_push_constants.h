#pragma once

#include <cstdint>

namespace jupiter {
namespace rendering {

/**
 * @brief Push constants for PBR shader runtime tuning
 * 
 * These values can be changed at runtime without recreating the pipeline.
 * Must be 128 bytes or less (Vulkan minimum guaranteed).
 */
struct PBRPushConstants {
    // Light intensity multipliers
    float directLightIntensity = 1.0f;   ///< Scale all direct lights
    float ambientIntensity = 1.0f;       ///< Scale ambient/IBL contribution
    float emissiveIntensity = 1.0f;      ///< Scale emissive contribution
    
    // Material overrides (when useOverride flags are set)
    float roughnessOverride = 0.5f;      ///< Override roughness for all materials
    float metallicOverride = 0.0f;       ///< Override metallic for all materials
    float baseReflectivity = 0.04f;      ///< F0 for dielectrics (default 4% reflectance)
    
    // Exposure and tonemapping
    float exposure = 1.0f;               ///< Manual exposure multiplier
    float shadowIntensity = 1.0f;        ///< Shadow darkness (0=no shadow, 1=full shadow)
    
    // IBL parameters (from HelloVulkan)
    float maxReflectionLod = 4.0f;       ///< Max mip level for prefiltered IBL map
    float lightFalloff = 1.0f;           ///< Light attenuation power (lower = slower falloff)
    float albedoMultiplier = 0.0f;       ///< Add albedo color to dark scenes (0=disabled)
    
    // Feature flags (packed as bits)
    uint32_t flags = 0;
    
    // Flag bits
    static constexpr uint32_t FLAG_USE_ROUGHNESS_OVERRIDE = 1 << 0;
    static constexpr uint32_t FLAG_USE_METALLIC_OVERRIDE = 1 << 1;
    static constexpr uint32_t FLAG_DISABLE_IBL = 1 << 2;
    static constexpr uint32_t FLAG_DISABLE_NORMAL_MAPPING = 1 << 3;
    static constexpr uint32_t FLAG_DEBUG_NORMALS = 1 << 4;
    static constexpr uint32_t FLAG_DEBUG_ALBEDO = 1 << 5;
    static constexpr uint32_t FLAG_DEBUG_METALLIC = 1 << 6;
    static constexpr uint32_t FLAG_DEBUG_ROUGHNESS = 1 << 7;
    static constexpr uint32_t FLAG_DEBUG_AO = 1 << 8;
    static constexpr uint32_t FLAG_DEBUG_UV = 1 << 9;
    static constexpr uint32_t FLAG_DEBUG_EMISSIVE = 1 << 10;
    static constexpr uint32_t FLAG_DISABLE_DIRECT_LIGHT = 1 << 11;
    static constexpr uint32_t FLAG_DISABLE_TONEMAPPING = 1 << 12;
    static constexpr uint32_t FLAG_DEBUG_DIFFUSE_ONLY = 1 << 13;
    static constexpr uint32_t FLAG_DEBUG_SPECULAR_ONLY = 1 << 14;
    static constexpr uint32_t FLAG_DEBUG_F0 = 1 << 15;
    
    // Helper methods
    void setRoughnessOverride(float roughness) {
        roughnessOverride = roughness;
        flags |= FLAG_USE_ROUGHNESS_OVERRIDE;
    }
    
    void setMetallicOverride(float metallic) {
        metallicOverride = metallic;
        flags |= FLAG_USE_METALLIC_OVERRIDE;
    }
    
    void clearRoughnessOverride() {
        flags &= ~FLAG_USE_ROUGHNESS_OVERRIDE;
    }
    
    void clearMetallicOverride() {
        flags &= ~FLAG_USE_METALLIC_OVERRIDE;
    }
    
    void setDebugMode(uint32_t debugFlag) {
        // Clear all debug flags first
        flags &= ~(FLAG_DEBUG_NORMALS | FLAG_DEBUG_ALBEDO | FLAG_DEBUG_METALLIC |
                   FLAG_DEBUG_ROUGHNESS | FLAG_DEBUG_AO);
        flags |= debugFlag;
    }
    
    void clearDebugMode() {
        flags &= ~(FLAG_DEBUG_NORMALS | FLAG_DEBUG_ALBEDO | FLAG_DEBUG_METALLIC |
                   FLAG_DEBUG_ROUGHNESS | FLAG_DEBUG_AO);
    }
    
    void disableIBL(bool disable) {
        if (disable) {
            flags |= FLAG_DISABLE_IBL;
        } else {
            flags &= ~FLAG_DISABLE_IBL;
        }
    }
    
    void disableNormalMapping(bool disable) {
        if (disable) {
            flags |= FLAG_DISABLE_NORMAL_MAPPING;
        } else {
            flags &= ~FLAG_DISABLE_NORMAL_MAPPING;
        }
    }
};

// Static assert moved outside class definition
static_assert(sizeof(PBRPushConstants) <= 128, "Push constants must be <= 128 bytes");

} // namespace rendering
} // namespace jupiter

