#pragma once

/**
 * @file render_features.h
 * @brief Runtime toggle system for rendering features
 * 
 * Allows enabling/disabling advanced rendering techniques at runtime:
 * - Clustered Forward Shading
 * - Screen Space Ambient Occlusion (SSAO)
 * - Shadow Mapping
 * - Multisample Anti-Aliasing (MSAA)
 * - GPU Frustum Culling
 * - Indirect Draw
 */

#include <cstdint>
#include <string>
#include <unordered_map>

namespace jupiter::rendering {

/**
 * @brief Feature flags for rendering capabilities
 */
enum class RenderFeature : uint32_t {
    None = 0,
    
    // Lighting
    ClusteredForward    = 1 << 0,   // Clustered forward shading (many lights)
    ForwardPlus         = 1 << 1,   // Forward+ (tiled deferred light culling)
    DeferredShading     = 1 << 2,   // Full deferred rendering
    
    // Shadows
    ShadowMapping       = 1 << 3,   // Basic shadow maps
    CascadedShadows     = 1 << 4,   // Cascaded shadow maps (CSM)
    SoftShadows         = 1 << 5,   // PCF/PCSS soft shadows
    
    // Effects
    SSAO                = 1 << 6,   // Screen-space ambient occlusion
    SSR                 = 1 << 7,   // Screen-space reflections
    Bloom               = 1 << 8,   // HDR bloom
    Tonemap             = 1 << 9,   // HDR tonemapping
    FXAA                = 1 << 10,  // Fast approximate AA
    
    // Anti-aliasing
    MSAA_2X             = 1 << 11,
    MSAA_4X             = 1 << 12,
    MSAA_8X             = 1 << 13,
    
    // GPU-driven rendering
    GPUFrustumCulling   = 1 << 14,  // Compute-based culling
    IndirectDraw        = 1 << 15,  // VkDrawIndexedIndirectCommand
    Bindless            = 1 << 16,  // Descriptor indexing
    
    // Advanced
    Raytracing          = 1 << 17,  // Hardware RT
    VRS                 = 1 << 18,  // Variable rate shading
    MeshShaders         = 1 << 19,  // Mesh shader pipeline
    
    // IBL
    ImageBasedLighting  = 1 << 20,  // Environment map lighting
    
    // Debug
    Wireframe           = 1 << 24,
    ShowAABBs           = 1 << 25,
    ShowLightVolumes    = 1 << 26,
    ShowClusters        = 1 << 27,
};

// Bitwise operators
inline RenderFeature operator|(RenderFeature a, RenderFeature b) {
    return static_cast<RenderFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RenderFeature operator&(RenderFeature a, RenderFeature b) {
    return static_cast<RenderFeature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline RenderFeature operator~(RenderFeature a) {
    return static_cast<RenderFeature>(~static_cast<uint32_t>(a));
}

inline bool hasFeature(RenderFeature mask, RenderFeature feature) {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(feature)) != 0;
}

/**
 * @brief Configuration for clustered forward shading
 */
struct ClusteredForwardConfig {
    uint32_t clusterCountX = 16;    // Clusters in X
    uint32_t clusterCountY = 9;     // Clusters in Y
    uint32_t clusterCountZ = 24;    // Clusters in depth (slices)
    uint32_t maxLightsPerCluster = 100;
    float zNear = 0.1f;
    float zFar = 1000.0f;
    
    uint32_t totalClusterCount() const {
        return clusterCountX * clusterCountY * clusterCountZ;
    }
};

/**
 * @brief Configuration for shadow mapping
 */
struct ShadowConfig {
    uint32_t shadowMapSize = 2048;
    uint32_t cascadeCount = 4;
    float cascadeSplitLambda = 0.95f;  // Logarithmic split factor
    float shadowBias = 0.005f;
    float normalBias = 0.02f;
    bool pcfEnabled = true;
    uint32_t pcfKernelSize = 3;
};

/**
 * @brief Configuration for SSAO
 */
struct SSAOConfig {
    uint32_t sampleCount = 64;
    float radius = 0.5f;
    float bias = 0.025f;
    float intensity = 1.0f;
    bool blurEnabled = true;
};

/**
 * @brief Render features manager
 * 
 * Tracks enabled features and their configurations.
 * Used by Application to determine which pipelines to create.
 */
class RenderFeatures {
public:
    RenderFeatures() = default;

    // ========================================================================
    // Feature Toggles
    // ========================================================================

    void enable(RenderFeature feature) {
        enabledFeatures_ = enabledFeatures_ | feature;
    }

    void disable(RenderFeature feature) {
        enabledFeatures_ = enabledFeatures_ & ~feature;
    }

    void toggle(RenderFeature feature) {
        if (isEnabled(feature)) {
            disable(feature);
        } else {
            enable(feature);
        }
    }

    [[nodiscard]] bool isEnabled(RenderFeature feature) const {
        return hasFeature(enabledFeatures_, feature);
    }

    [[nodiscard]] RenderFeature getEnabled() const {
        return enabledFeatures_;
    }

    void setEnabled(RenderFeature features) {
        enabledFeatures_ = features;
    }

    // ========================================================================
    // Feature Configurations
    // ========================================================================

    ClusteredForwardConfig& clusteredConfig() { return clusteredConfig_; }
    const ClusteredForwardConfig& clusteredConfig() const { return clusteredConfig_; }

    ShadowConfig& shadowConfig() { return shadowConfig_; }
    const ShadowConfig& shadowConfig() const { return shadowConfig_; }

    SSAOConfig& ssaoConfig() { return ssaoConfig_; }
    const SSAOConfig& ssaoConfig() const { return ssaoConfig_; }

    // ========================================================================
    // Queries
    // ========================================================================

    /**
     * @brief Check if any MSAA is enabled
     */
    [[nodiscard]] bool isMSAAEnabled() const {
        return isEnabled(RenderFeature::MSAA_2X) ||
               isEnabled(RenderFeature::MSAA_4X) ||
               isEnabled(RenderFeature::MSAA_8X);
    }

    /**
     * @brief Get MSAA sample count
     */
    [[nodiscard]] uint32_t getMSAASamples() const {
        if (isEnabled(RenderFeature::MSAA_8X)) return 8;
        if (isEnabled(RenderFeature::MSAA_4X)) return 4;
        if (isEnabled(RenderFeature::MSAA_2X)) return 2;
        return 1;
    }

    /**
     * @brief Check if GPU-driven rendering is enabled
     */
    [[nodiscard]] bool isGPUDriven() const {
        return isEnabled(RenderFeature::GPUFrustumCulling) ||
               isEnabled(RenderFeature::IndirectDraw);
    }

    /**
     * @brief Check if any debug visualization is enabled
     */
    [[nodiscard]] bool isDebugVisualization() const {
        return isEnabled(RenderFeature::Wireframe) ||
               isEnabled(RenderFeature::ShowAABBs) ||
               isEnabled(RenderFeature::ShowLightVolumes) ||
               isEnabled(RenderFeature::ShowClusters);
    }

    // ========================================================================
    // Presets
    // ========================================================================

    /**
     * @brief Minimum quality preset (for low-end hardware)
     */
    static RenderFeatures lowQuality() {
        RenderFeatures f;
        f.enable(RenderFeature::Tonemap);
        return f;
    }

    /**
     * @brief Medium quality preset
     */
    static RenderFeatures mediumQuality() {
        RenderFeatures f;
        f.enable(RenderFeature::ClusteredForward);
        f.enable(RenderFeature::ShadowMapping);
        f.enable(RenderFeature::Tonemap);
        f.enable(RenderFeature::ImageBasedLighting);
        f.enable(RenderFeature::FXAA);
        return f;
    }

    /**
     * @brief High quality preset
     */
    static RenderFeatures highQuality() {
        RenderFeatures f;
        f.enable(RenderFeature::ClusteredForward);
        f.enable(RenderFeature::CascadedShadows);
        f.enable(RenderFeature::SoftShadows);
        f.enable(RenderFeature::SSAO);
        f.enable(RenderFeature::Bloom);
        f.enable(RenderFeature::Tonemap);
        f.enable(RenderFeature::ImageBasedLighting);
        f.enable(RenderFeature::MSAA_4X);
        f.enable(RenderFeature::GPUFrustumCulling);
        return f;
    }

    /**
     * @brief Ultra quality preset (for high-end hardware)
     */
    static RenderFeatures ultraQuality() {
        RenderFeatures f = highQuality();
        f.disable(RenderFeature::MSAA_4X);
        f.enable(RenderFeature::MSAA_8X);
        f.enable(RenderFeature::IndirectDraw);
        f.enable(RenderFeature::Bindless);
        return f;
    }

private:
    RenderFeature enabledFeatures_ = RenderFeature::None;
    
    ClusteredForwardConfig clusteredConfig_;
    ShadowConfig shadowConfig_;
    SSAOConfig ssaoConfig_;
};

} // namespace jupiter::rendering

