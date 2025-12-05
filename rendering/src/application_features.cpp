/**
 * @file application_features.cpp
 * @brief Advanced rendering features integration implementation
 */

#include "rendering/application_features.h"
#include "rendering/scene_manager.h"
#include "rendering/texture.h"
#include "logging/logging.h"

namespace jupiter::rendering {

ApplicationFeatures::~ApplicationFeatures() {
    destroy();
}

void ApplicationFeatures::initialize(VkDevice device,
                                      VkPhysicalDevice physicalDevice,
                                      VkRenderPass swapchainRenderPass,
                                      VkImageView depthImageView,
                                      uint32_t width,
                                      uint32_t height,
                                      uint32_t framesInFlight) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    swapchainRenderPass_ = swapchainRenderPass;
    depthImageView_ = depthImageView;
    width_ = width;
    height_ = height;
    framesInFlight_ = framesInFlight;

    LOG_INFO("ApplicationFeatures", "Initializing advanced rendering features");

    // Create resources based on enabled features
    if (features_.isEnabled(RenderFeature::ShadowMapping)) {
        createShadowResources();
    }

    if (features_.isEnabled(RenderFeature::SSAO)) {
        createSSAOResources();
    }

    if (features_.isEnabled(RenderFeature::Tonemap)) {
        createHDRResources();
    }

    if (features_.isEnabled(RenderFeature::ImageBasedLighting) && envCubemap_) {
        createSkyboxPipeline();
    }

    initialized_ = true;
    LOG_INFO("ApplicationFeatures", "Advanced rendering features initialized");
}

void ApplicationFeatures::destroy() {
    if (!initialized_) return;

    LOG_INFO("ApplicationFeatures", "Destroying advanced rendering features");

    destroySkyboxPipeline();
    destroyHDRResources();
    destroySSAOResources();
    destroyShadowResources();

    initialized_ = false;
}

void ApplicationFeatures::onWindowResized(uint32_t width, uint32_t height, VkImageView newDepthView) {
    width_ = width;
    height_ = height;
    depthImageView_ = newDepthView;

    if (resourcesGBuffer_) {
        resourcesGBuffer_->onWindowResized(width, height);
    }

    if (resourcesHDR_) {
        resourcesHDR_->onWindowResized(width, height);
        resourcesHDR_->setDepthView(newDepthView);
    }

    if (pipelineSkybox_) {
        pipelineSkybox_->setViewportSize(width, height);
    }

    if (pipelineSSAO_) {
        pipelineSSAO_->onWindowResized(width, height);
    }

    if (pipelineTonemap_) {
        pipelineTonemap_->onWindowResized(width, height);
    }
}

void ApplicationFeatures::enableFeature(RenderFeature feature) {
    if (features_.isEnabled(feature)) return;

    features_.enable(feature);

    if (!initialized_) return;

    switch (feature) {
        case RenderFeature::ShadowMapping:
            if (!resourcesShadow_) createShadowResources();
            break;
        case RenderFeature::SSAO:
            if (!resourcesGBuffer_) createSSAOResources();
            break;
        case RenderFeature::Tonemap:
            if (!resourcesHDR_) createHDRResources();
            break;
        case RenderFeature::ImageBasedLighting:
            if (!pipelineSkybox_ && envCubemap_) createSkyboxPipeline();
            break;
        default:
            break;
    }
}

void ApplicationFeatures::disableFeature(RenderFeature feature) {
    if (!features_.isEnabled(feature)) return;

    features_.disable(feature);

    switch (feature) {
        case RenderFeature::ShadowMapping:
            destroyShadowResources();
            break;
        case RenderFeature::SSAO:
            destroySSAOResources();
            break;
        case RenderFeature::Tonemap:
            destroyHDRResources();
            break;
        case RenderFeature::ImageBasedLighting:
            destroySkyboxPipeline();
            break;
        default:
            break;
    }
}

void ApplicationFeatures::setSceneManager(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;

    if (pipelineShadow_) {
        pipelineShadow_->setSceneManager(sceneManager);
    }

    if (pipelineGBuffer_) {
        pipelineGBuffer_->setSceneManager(sceneManager);
    }
}

void ApplicationFeatures::setEnvironmentMap(VulkanTexture* envCubemap) {
    envCubemap_ = envCubemap;

    if (pipelineSkybox_) {
        pipelineSkybox_->setEnvironmentMap(envCubemap);
    } else if (initialized_ && features_.isEnabled(RenderFeature::ImageBasedLighting)) {
        createSkyboxPipeline();
    }
}

void ApplicationFeatures::updateCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) {
    if (pipelineGBuffer_) {
        pipelineGBuffer_->setCameraUBO(ubo, frameIndex);
    }

    if (pipelineSSAO_) {
        pipelineSSAO_->setCameraUBO(ubo, frameIndex);
    }

    if (pipelineSkybox_) {
        pipelineSkybox_->setCameraUBO(ubo, frameIndex);
    }
}

void ApplicationFeatures::updateShadowLight(const glm::vec3& lightPos,
                                             const glm::vec3& lightDir,
                                             const glm::vec3& targetPos) {
    if (pipelineShadow_) {
        pipelineShadow_->updateShadow(lightPos, lightDir, targetPos);
    }
}

void ApplicationFeatures::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!isFeatureActive(RenderFeature::ShadowMapping) || !pipelineShadow_) return;

    pipelineShadow_->fillCommandBuffer(cmd, frameIndex);
}

void ApplicationFeatures::recordGBufferPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!isFeatureActive(RenderFeature::SSAO) || !pipelineGBuffer_) return;

    pipelineGBuffer_->fillCommandBuffer(cmd, frameIndex);
}

void ApplicationFeatures::recordSSAOPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!isFeatureActive(RenderFeature::SSAO) || !pipelineSSAO_) return;

    pipelineSSAO_->fillCommandBuffer(cmd, frameIndex);
}

void ApplicationFeatures::recordSkybox(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!isFeatureActive(RenderFeature::ImageBasedLighting) || !pipelineSkybox_) return;

    pipelineSkybox_->fillCommandBuffer(cmd, frameIndex);
}

void ApplicationFeatures::recordTonemapPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!isFeatureActive(RenderFeature::Tonemap) || !pipelineTonemap_) return;

    pipelineTonemap_->fillCommandBuffer(cmd, frameIndex);
}

bool ApplicationFeatures::isFeatureActive(RenderFeature feature) const {
    if (!features_.isEnabled(feature)) return false;

    switch (feature) {
        case RenderFeature::ShadowMapping:
            return resourcesShadow_ != nullptr && pipelineShadow_ != nullptr;
        case RenderFeature::SSAO:
            return resourcesGBuffer_ != nullptr && pipelineSSAO_ != nullptr;
        case RenderFeature::Tonemap:
            return resourcesHDR_ != nullptr && pipelineTonemap_ != nullptr;
        case RenderFeature::ImageBasedLighting:
            return pipelineSkybox_ != nullptr && envCubemap_ != nullptr;
        default:
            return false;
    }
}

VkDescriptorImageInfo ApplicationFeatures::getShadowMapDescriptor() const {
    if (resourcesShadow_) {
        return resourcesShadow_->getShadowMapDescriptor();
    }
    return {};
}

VkDescriptorImageInfo ApplicationFeatures::getSSAODescriptor() const {
    if (resourcesGBuffer_) {
        return resourcesGBuffer_->getSSAOTexture().getDescriptorInfo(
            resourcesGBuffer_->getSampler());
    }
    return {};
}

glm::mat4 ApplicationFeatures::getLightSpaceMatrix() const {
    if (resourcesShadow_) {
        return resourcesShadow_->getLightSpaceMatrix();
    }
    return glm::mat4(1.0f);
}

// ============================================================================
// Private Implementation
// ============================================================================

void ApplicationFeatures::createShadowResources() {
    LOG_INFO("ApplicationFeatures", "Creating shadow mapping resources");

    resourcesShadow_ = std::make_unique<ResourcesShadow>();

    ShadowMapConfig shadowConfig;
    shadowConfig.resolution = features_.shadowConfig().shadowMapSize;
    shadowConfig.minBias = features_.shadowConfig().shadowBias;
    shadowConfig.maxBias = features_.shadowConfig().normalBias;

    resourcesShadow_->create(device_, physicalDevice_, shadowConfig, framesInFlight_);

    PipelineConfig pipelineConfig;
    pipelineConfig.type = PipelineType::GraphicsOffScreen;
    pipelineConfig.name = "ShadowPass";

    pipelineShadow_ = std::make_unique<PipelineShadow>(
        device_, physicalDevice_, pipelineConfig, resourcesShadow_.get());

    if (sceneManager_) {
        pipelineShadow_->setSceneManager(sceneManager_);
    }
}

void ApplicationFeatures::createSSAOResources() {
    LOG_INFO("ApplicationFeatures", "Creating SSAO resources");

    resourcesGBuffer_ = std::make_unique<ResourcesGBuffer>();

    GBufferConfig gBufferConfig;
    gBufferConfig.width = width_;
    gBufferConfig.height = height_;
    gBufferConfig.ssaoKernelSize = features_.ssaoConfig().sampleCount;

    resourcesGBuffer_->create(device_, physicalDevice_, gBufferConfig, framesInFlight_);

    // G-buffer pipeline
    PipelineConfig gBufferPipelineConfig;
    gBufferPipelineConfig.type = PipelineType::GraphicsOffScreen;
    gBufferPipelineConfig.name = "GBufferPass";

    pipelineGBuffer_ = std::make_unique<PipelineGBuffer>(
        device_, physicalDevice_, gBufferPipelineConfig, resourcesGBuffer_.get());

    if (sceneManager_) {
        pipelineGBuffer_->setSceneManager(sceneManager_);
    }

    // SSAO pipeline
    PipelineConfig ssaoPipelineConfig;
    ssaoPipelineConfig.type = PipelineType::GraphicsOffScreen;
    ssaoPipelineConfig.name = "SSAOPass";

    pipelineSSAO_ = std::make_unique<PipelineSSAO>(
        device_, physicalDevice_, ssaoPipelineConfig, resourcesGBuffer_.get());

    pipelineSSAO_->setParameters(
        features_.ssaoConfig().radius,
        features_.ssaoConfig().bias,
        features_.ssaoConfig().intensity);
}

void ApplicationFeatures::createHDRResources() {
    LOG_INFO("ApplicationFeatures", "Creating HDR/tonemap resources");

    resourcesHDR_ = std::make_unique<ResourcesHDR>();

    HDRConfig hdrConfig;
    hdrConfig.width = width_;
    hdrConfig.height = height_;

    resourcesHDR_->create(device_, physicalDevice_, hdrConfig, depthImageView_);

    PipelineConfig tonemapConfig;
    tonemapConfig.type = PipelineType::GraphicsOnScreen;
    tonemapConfig.name = "TonemapPass";

    pipelineTonemap_ = std::make_unique<PipelineTonemap>(
        device_, physicalDevice_, tonemapConfig, resourcesHDR_.get(), swapchainRenderPass_);
}

void ApplicationFeatures::createSkyboxPipeline() {
    if (!envCubemap_) {
        LOG_WARN("ApplicationFeatures", "Cannot create skybox pipeline: no environment cubemap");
        return;
    }

    LOG_INFO("ApplicationFeatures", "Creating skybox pipeline");

    PipelineConfig skyboxConfig;
    skyboxConfig.type = PipelineType::GraphicsOnScreen;
    skyboxConfig.name = "SkyboxPass";

    pipelineSkybox_ = std::make_unique<PipelineSkybox>(
        device_, physicalDevice_, skyboxConfig, swapchainRenderPass_, envCubemap_);

    pipelineSkybox_->setViewportSize(width_, height_);
}

void ApplicationFeatures::destroyShadowResources() {
    pipelineShadow_.reset();
    resourcesShadow_.reset();
}

void ApplicationFeatures::destroySSAOResources() {
    pipelineSSAO_.reset();
    pipelineGBuffer_.reset();
    resourcesGBuffer_.reset();
}

void ApplicationFeatures::destroyHDRResources() {
    pipelineTonemap_.reset();
    resourcesHDR_.reset();
}

void ApplicationFeatures::destroySkyboxPipeline() {
    pipelineSkybox_.reset();
}

} // namespace jupiter::rendering

