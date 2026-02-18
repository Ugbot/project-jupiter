/**
 * @file ibl_processor.cpp
 * @brief GHI-based IBL processor implementation
 */

#include "rendering/ral/ibl_processor.h"
#include "logging/logging.h"

namespace jupiter {
namespace rendering {
namespace ral {

IBLProcessor::~IBLProcessor() {
    shutdown();
}

void IBLProcessor::shutdown() {
    if (!initialized_) return;

    LOG_INFO("IBLProcessor", "Shutting down IBL processor");

    // Destroy shaders
    if (brdfLutShader_.isValid()) ghi::destroyShader(brdfLutShader_);
    if (irradianceShader_.isValid()) ghi::destroyShader(irradianceShader_);
    if (prefilteredShader_.isValid()) ghi::destroyShader(prefilteredShader_);

    // Destroy textures
    if (brdfLUT_.isValid()) ghi::destroyTexture(brdfLUT_);
    if (irradianceMap_.isValid()) ghi::destroyTexture(irradianceMap_);
    if (prefilteredMap_.isValid()) ghi::destroyTexture(prefilteredMap_);

    initialized_ = false;
}

bool IBLProcessor::initialize(const IBLConfig& config) {
    config_ = config;

    LOG_INFO("IBLProcessor", "Initializing GHI IBL processor");

    // Check compute shader support
    if (!ghi::hasComputeShaders()) {
        LOG_ERROR("IBLProcessor", "Compute shaders not supported - IBL requires compute");
        return false;
    }

    // Load compute shaders
    if (!loadShaders()) {
        LOG_WARN("IBLProcessor", "Some IBL shaders not found - functionality will be limited");
    }

    initialized_ = true;
    LOG_INFO("IBLProcessor", "IBL processor initialized");
    return true;
}

bool IBLProcessor::loadShaders() {
    bool allLoaded = true;

    // BRDF LUT compute shader
    ghi::ShaderSource brdfSource;
    brdfSource.computePath = "rendering/shaders/ibl/brdf_lut.comp.spv";
    brdfLutShader_ = ghi::createComputeShader(brdfSource);
    if (!brdfLutShader_.isValid()) {
        LOG_WARN("IBLProcessor", "BRDF LUT shader not found at %s", brdfSource.computePath);
        allLoaded = false;
    }

    // Irradiance convolution shader (TODO: create compute version)
    // Currently using fragment shader approach
    LOG_INFO("IBLProcessor", "Irradiance shader: using fallback (compute version TODO)");

    // Prefiltered environment shader (TODO: create compute version)
    LOG_INFO("IBLProcessor", "Prefiltered shader: using fallback (compute version TODO)");

    LOG_INFO("IBLProcessor", "Loaded IBL shaders (BRDF: %s)",
             brdfLutShader_.isValid() ? "yes" : "no");
    return allLoaded;
}

ghi::TextureHandle IBLProcessor::generateBRDFLUT() {
    if (!initialized_) {
        LOG_ERROR("IBLProcessor", "Not initialized");
        return ghi::TextureHandle{};
    }

    // Return cached LUT if already generated
    if (brdfLUT_.isValid()) {
        return brdfLUT_;
    }

    if (!brdfLutShader_.isValid()) {
        LOG_ERROR("IBLProcessor", "BRDF LUT shader not loaded");
        return ghi::TextureHandle{};
    }

    LOG_INFO("IBLProcessor", "Generating BRDF LUT (%ux%u)...", 
             config_.brdfLutSize, config_.brdfLutSize);

    // Create output texture (RG16F for scale/bias)
    ghi::TextureCreateInfo lutInfo;
    lutInfo.type = ghi::TextureType::Texture2D;
    lutInfo.format = ghi::Format::RG16_FLOAT;
    lutInfo.width = config_.brdfLutSize;
    lutInfo.height = config_.brdfLutSize;
    lutInfo.mipLevels = 1;
    lutInfo.usage = ghi::TextureUsage::Sampled | ghi::TextureUsage::Storage;
    lutInfo.minFilter = ghi::Filter::Linear;
    lutInfo.magFilter = ghi::Filter::Linear;
    lutInfo.wrapS = ghi::WrapMode::ClampToEdge;
    lutInfo.wrapT = ghi::WrapMode::ClampToEdge;

    brdfLUT_ = ghi::createTexture(lutInfo);
    if (!brdfLUT_.isValid()) {
        LOG_ERROR("IBLProcessor", "Failed to create BRDF LUT texture");
        return ghi::TextureHandle{};
    }

    // Bind compute shader
    ghi::bindComputeShader(brdfLutShader_);

    // Bind output texture as storage image
    ghi::bindStorageTexture(brdfLUT_, 0, 0);

    // Dispatch compute shader (16x16 workgroups)
    uint32_t groupsX = (config_.brdfLutSize + 15) / 16;
    uint32_t groupsY = (config_.brdfLutSize + 15) / 16;
    ghi::dispatch(groupsX, groupsY, 1);

    // Memory barrier to ensure writes complete
    ghi::memoryBarrier();

    LOG_INFO("IBLProcessor", "BRDF LUT generated successfully");
    return brdfLUT_;
}

ghi::TextureHandle IBLProcessor::generateIrradianceMap(ghi::TextureHandle environmentMap) {
    if (!initialized_) {
        LOG_ERROR("IBLProcessor", "Not initialized");
        return ghi::TextureHandle{};
    }

    if (!environmentMap.isValid()) {
        LOG_ERROR("IBLProcessor", "Invalid environment map");
        return ghi::TextureHandle{};
    }

    // TODO: Implement irradiance convolution with compute shader
    // For now, return a stub
    LOG_WARN("IBLProcessor", "Irradiance map generation not yet implemented - using stub");

    // Create placeholder irradiance cubemap
    ghi::TextureCreateInfo irrInfo;
    irrInfo.type = ghi::TextureType::TextureCube;
    irrInfo.format = ghi::Format::RGBA16_FLOAT;
    irrInfo.width = config_.irradianceSize;
    irrInfo.height = config_.irradianceSize;
    irrInfo.depth = 6;  // 6 cube faces
    irrInfo.mipLevels = 1;
    irrInfo.usage = ghi::TextureUsage::Sampled | ghi::TextureUsage::Storage;

    irradianceMap_ = ghi::createTexture(irrInfo);
    return irradianceMap_;
}

ghi::TextureHandle IBLProcessor::generatePrefilteredMap(ghi::TextureHandle environmentMap) {
    if (!initialized_) {
        LOG_ERROR("IBLProcessor", "Not initialized");
        return ghi::TextureHandle{};
    }

    if (!environmentMap.isValid()) {
        LOG_ERROR("IBLProcessor", "Invalid environment map");
        return ghi::TextureHandle{};
    }

    // TODO: Implement prefiltered environment map with compute shader
    // For now, return a stub
    LOG_WARN("IBLProcessor", "Prefiltered map generation not yet implemented - using stub");

    // Create placeholder prefiltered cubemap with mips
    ghi::TextureCreateInfo prefInfo;
    prefInfo.type = ghi::TextureType::TextureCube;
    prefInfo.format = ghi::Format::RGBA16_FLOAT;
    prefInfo.width = config_.prefilteredSize;
    prefInfo.height = config_.prefilteredSize;
    prefInfo.depth = 6;  // 6 cube faces
    prefInfo.mipLevels = config_.prefilteredMipLevels;
    prefInfo.usage = ghi::TextureUsage::Sampled | ghi::TextureUsage::Storage;

    prefilteredMap_ = ghi::createTexture(prefInfo);
    return prefilteredMap_;
}

} // namespace ral
} // namespace rendering
} // namespace jupiter
