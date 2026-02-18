#pragma once

/**
 * @file ibl_processor.h
 * @brief GHI-based IBL (Image-Based Lighting) processor
 * 
 * Generates environment maps for PBR rendering:
 * - BRDF LUT (precomputed lookup table)
 * - Irradiance map (diffuse convolution)
 * - Prefiltered environment map (specular convolution)
 */

#include "rendering/ghi/ghi.h"
#include <glm/glm.hpp>

namespace jupiter {
namespace rendering {
namespace ral {

/**
 * @brief IBL configuration
 */
struct IBLConfig {
    uint32_t brdfLutSize = 512;           // BRDF LUT resolution (square)
    uint32_t irradianceSize = 32;         // Irradiance map face size
    uint32_t prefilteredSize = 128;       // Prefiltered map base mip size
    uint32_t prefilteredMipLevels = 5;    // Number of roughness mip levels
};

/**
 * @brief IBL environment map processor
 * 
 * Uses GHI compute shaders to generate:
 * 1. BRDF Integration LUT (split-sum approximation)
 * 2. Irradiance convolution (diffuse IBL)
 * 3. Prefiltered environment (specular IBL with roughness mips)
 */
class IBLProcessor {
public:
    IBLProcessor() = default;
    ~IBLProcessor();

    // Non-copyable
    IBLProcessor(const IBLProcessor&) = delete;
    IBLProcessor& operator=(const IBLProcessor&) = delete;

    /**
     * @brief Initialize IBL processor
     * 
     * @param config IBL configuration
     * @return true if successful
     */
    bool initialize(const IBLConfig& config = IBLConfig{});

    /**
     * @brief Shutdown and release resources
     */
    void shutdown();

    /**
     * @brief Generate BRDF integration LUT
     * 
     * Call once at startup - result is environment-independent.
     * 
     * @return BRDF LUT texture handle
     */
    ghi::TextureHandle generateBRDFLUT();

    /**
     * @brief Generate irradiance map from environment cubemap
     * 
     * @param environmentMap Source HDR environment cubemap
     * @return Irradiance cubemap handle
     */
    ghi::TextureHandle generateIrradianceMap(ghi::TextureHandle environmentMap);

    /**
     * @brief Generate prefiltered environment map with roughness mips
     * 
     * @param environmentMap Source HDR environment cubemap
     * @return Prefiltered environment cubemap with mip chain
     */
    ghi::TextureHandle generatePrefilteredMap(ghi::TextureHandle environmentMap);

    /**
     * @brief Get BRDF LUT (must call generateBRDFLUT first)
     */
    ghi::TextureHandle getBRDFLUT() const { return brdfLUT_; }

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    bool initialized_ = false;
    IBLConfig config_;

    // Compute shaders
    ghi::ShaderHandle brdfLutShader_;
    ghi::ShaderHandle irradianceShader_;
    ghi::ShaderHandle prefilteredShader_;

    // Generated textures
    ghi::TextureHandle brdfLUT_;
    ghi::TextureHandle irradianceMap_;
    ghi::TextureHandle prefilteredMap_;

    bool loadShaders();
};

} // namespace ral
} // namespace rendering
} // namespace jupiter
