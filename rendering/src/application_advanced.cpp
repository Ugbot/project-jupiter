/**
 * @file application_advanced.cpp
 * @brief Implementation of ApplicationAdvanced with ECS and clustered rendering
 */

#include "rendering/application_advanced.h"
#include "rendering/pipeline_base.h"  // for CameraUBO
#include "ecs/builtin_kernels.h"
#include "vulkan_backend.h"
#include "logging/logging.h"
#include <cstring>  // for std::memcpy

namespace jupiter::rendering {

// ============================================================================
// Construction/Destruction
// ============================================================================

ApplicationAdvanced::ApplicationAdvanced(const std::string& title, uint32_t width, uint32_t height,
                                        bool enableValidation)
    : Application(title, width, height, enableValidation) {
}

ApplicationAdvanced::~ApplicationAdvanced() {
    // Cleanup handled by onShutdown and unique_ptrs
}

// ============================================================================
// Lifecycle
// ============================================================================

void ApplicationAdvanced::onInit() {
    // Initialize ECS
    initializeECS();

    // Call derived class initialization first (so they can configure features)
    onInitAdvanced();

    // Initialize advanced rendering features (shadow, SSAO, HDR, skybox)
    initializeAdvancedFeatures();

    // Initialize clustered forward if enabled
    if (appFeatures_.features().isEnabled(RenderFeature::ClusteredForward)) {
        initializeClusteredForward();
    }
}

void ApplicationAdvanced::onUpdate(float deltaTime) {
    // Update kernel context
    kernelContext_.deltaTime = deltaTime;
    kernelContext_.frameNumber++;
    kernelContext_.generation = world_->generation();

    // Execute ECS kernels
    executeECSKernels(deltaTime);

    // Process queued entity operations
    world_->processQueued();

    // Swap ECS buffers (readers now see updated data)
    world_->swap();

    // Post-kernel hook for derived classes
    onPostKernels();
}

void ApplicationAdvanced::onRender() {
    // Execute clustered forward compute passes
    if (clusteredInitialized_ && appFeatures_.features().isEnabled(RenderFeature::ClusteredForward)) {
        executeClusteredPasses();
    }

    // Render entities from ECS
    renderFromECS();

    // Custom rendering from derived class
    onRenderAdvanced();
}

void ApplicationAdvanced::onShutdown() {
    // Call derived class cleanup first
    onShutdownAdvanced();

    // Destroy custom pipelines and resources
    pipelines_.clear();
    resources_.clear();

    // Destroy clustered forward
    clusteredPipelines_.reset();

    // Destroy advanced features (shadow, SSAO, HDR, skybox)
    appFeatures_.destroy();

    // Destroy ECS world
    world_.reset();
}

void ApplicationAdvanced::onPreRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!featuresInitialized_) return;

    // Update camera UBO for feature pipelines
    Camera* camera = getActiveCamera();
    if (camera) {
        CameraUBO cameraUBO{};
        
        // Copy view/projection matrices from internal math types to glm
        const math::Matrix4x4& viewMat = camera->getViewMatrix();
        const math::Matrix4x4& projMat = camera->getProjectionMatrix();
        std::memcpy(&cameraUBO.view[0][0], viewMat.data(), sizeof(float) * 16);
        std::memcpy(&cameraUBO.projection[0][0], projMat.data(), sizeof(float) * 16);
        cameraUBO.viewProjection = cameraUBO.projection * cameraUBO.view;
        
        // Copy camera position from internal math::Vector3
        const math::Vector3& pos = camera->getPosition();
        cameraUBO.cameraPosition = glm::vec4(pos.x, pos.y, pos.z, 1.0f);
        
        // Near/far/fov info
        cameraUBO.nearFarFov = glm::vec4(0.1f, 1000.0f, 1.0f, 1.78f);  // Defaults
        
        appFeatures_.updateCameraUBO(cameraUBO, frameIndex);
    }

    // Record shadow pass (renders to shadow map)
    appFeatures_.recordShadowPass(cmd, frameIndex);

    // Record G-buffer pass (renders position/normal for SSAO)
    appFeatures_.recordGBufferPass(cmd, frameIndex);

    // Record SSAO pass (computes ambient occlusion)
    appFeatures_.recordSSAOPass(cmd, frameIndex);

    // Update shadow effects UBO with enable flags and light space matrix
    // (texture bindings are done once during initialization, not per-frame)
    RenderGlobals* globals = getRenderGlobals();
    if (globals) {
        glm::mat4 lightSpaceMatrix = appFeatures_.getLightSpaceMatrix();
        bool shadowEnabled = appFeatures_.isFeatureActive(RenderFeature::ShadowMapping);
        bool ssaoEnabled = appFeatures_.isFeatureActive(RenderFeature::SSAO);
        float ssaoIntensity = appFeatures_.features().ssaoConfig().intensity;
        float shadowBias = appFeatures_.features().shadowConfig().shadowBias;

        globals->updateShadowEffects(frameIndex, lightSpaceMatrix, shadowEnabled, ssaoEnabled,
                                     ssaoIntensity, shadowBias);
    }
}

void ApplicationAdvanced::onPostRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!featuresInitialized_) return;

    // Record tonemap pass (converts HDR to LDR for display)
    appFeatures_.recordTonemapPass(cmd, frameIndex);
}

// ============================================================================
// ECS Integration
// ============================================================================

void ApplicationAdvanced::initializeECS() {
    // Register built-in kernels
    ecs::kernels::registerBuiltinKernels();

    // Create world with reasonable default capacity
    constexpr uint32_t DEFAULT_ENTITY_CAPACITY = 10000;
    world_ = std::make_unique<ecs::World>(DEFAULT_ENTITY_CAPACITY);

    // Initialize kernel context
    kernelContext_.deltaTime = 0.0f;
    kernelContext_.frameNumber = 0;
    kernelContext_.generation = 0;
    kernelContext_.vulkanContext = nullptr;  // Set by renderer if needed
    kernelContext_.userContext = nullptr;

    ecsInitialized_ = true;
}

void ApplicationAdvanced::initializeAdvancedFeatures() {
    vulkan::VulkanRenderer* renderer = getRenderer();
    if (!renderer) {
        LOG_ERROR("ApplicationAdvanced", "Cannot initialize advanced features: no renderer");
        return;
    }

    LOG_INFO("ApplicationAdvanced", "Initializing advanced rendering features");

    // Connect scene manager to features
    appFeatures_.setSceneManager(getSceneManager());

    // Initialize with Vulkan handles
    VkDevice device = renderer->getDevice();
    VkPhysicalDevice physicalDevice = renderer->getPhysicalDevice();
    VkRenderPass swapchainRenderPass = renderer->getRenderPass();
    VkImageView depthImageView = renderer->getDepthImageView();
    VkExtent2D extent = renderer->getExtent();
    uint32_t framesInFlight = renderer->getFramesInFlight();

    appFeatures_.initialize(
        device,
        physicalDevice,
        swapchainRenderPass,
        depthImageView,
        extent.width,
        extent.height,
        framesInFlight
    );

    featuresInitialized_ = true;

    // Bind shadow/SSAO textures to RenderGlobals (done once, not per-frame)
    RenderGlobals* globals = getRenderGlobals();
    if (globals) {
        // Bind shadow map texture (if feature is active)
        if (appFeatures_.isFeatureActive(RenderFeature::ShadowMapping)) {
            VkDescriptorImageInfo shadowInfo = appFeatures_.getShadowMapDescriptor();
            if (shadowInfo.imageView != VK_NULL_HANDLE && shadowInfo.sampler != VK_NULL_HANDLE) {
                globals->updateShadowMap(shadowInfo.imageView, shadowInfo.sampler);
            }
        }

        // Bind SSAO texture (if feature is active)
        if (appFeatures_.isFeatureActive(RenderFeature::SSAO)) {
            VkDescriptorImageInfo ssaoInfo = appFeatures_.getSSAODescriptor();
            if (ssaoInfo.imageView != VK_NULL_HANDLE && ssaoInfo.sampler != VK_NULL_HANDLE) {
                globals->updateSSAOTexture(ssaoInfo.imageView, ssaoInfo.sampler);
            }
        }
    }

    LOG_INFO("ApplicationAdvanced", "Advanced rendering features initialized");
}

void ApplicationAdvanced::executeECSKernels(float deltaTime) {
    if (!ecsInitialized_) return;

    auto& registry = ecs::KernelRegistry::instance();

    // Execute physics kernels (if physics entities exist)
    registry.execute("clear_forces", *world_, kernelContext_);
    registry.execute("gravity", *world_, kernelContext_);
    registry.execute("apply_forces", *world_, kernelContext_);
    registry.execute("physics_integrate", *world_, kernelContext_);
    registry.execute("physics_rotate", *world_, kernelContext_);

    // Update transforms
    registry.execute("transform", *world_, kernelContext_);

    // Update AABBs for culling
    registry.execute("compute_aabbs", *world_, kernelContext_);
}

uint32_t ApplicationAdvanced::createEntity(const glm::vec3& position,
                                           uint32_t meshId,
                                           uint32_t materialId) {
    ecs::EntityCreateInfo info;
    info.position = position;
    info.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    info.scale = glm::vec3(1.0f);
    info.meshId = meshId;
    info.materialId = materialId;
    info.flags = ecs::EntityFlags::Active;

    return world_->create(info);
}

uint32_t ApplicationAdvanced::createPhysicsEntity(const glm::vec3& position,
                                                  float mass,
                                                  uint32_t meshId,
                                                  uint32_t materialId) {
    ecs::EntityCreateInfo info;
    info.position = position;
    info.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    info.scale = glm::vec3(1.0f);
    info.mass = mass;
    info.meshId = meshId;
    info.materialId = materialId;
    info.flags = ecs::EntityFlags::Active | ecs::EntityFlags::Physics;

    return world_->create(info);
}

// ============================================================================
// Clustered Forward
// ============================================================================

void ApplicationAdvanced::initializeClusteredForward() {
    vulkan::VulkanRenderer* renderer = getRenderer();
    if (!renderer) {
        LOG_WARN("ApplicationAdvanced", "Cannot initialize clustered forward: no renderer");
        clusteredInitialized_ = false;
        return;
    }

    // TODO: Complete clustered forward initialization when needed
    // VkDevice device = renderer->getDevice();
    // VkPhysicalDevice physicalDevice = renderer->getPhysicalDevice();
    // clusteredPipelines_ = std::make_unique<ClusteredForwardPipelines>();
    // clusteredPipelines_->create(device, physicalDevice, appFeatures_.features().clusteredConfig(), maxLights);
    
    clusteredInitialized_ = false;  // Not fully implemented yet
}

void ApplicationAdvanced::executeClusteredPasses() {
    if (!clusteredPipelines_ || !clusteredPipelines_->isValid()) {
        return;
    }

    // Get camera matrices
    Camera* camera = getActiveCamera();
    if (!camera) return;

    // Convert from math::Matrix4x4 to glm::mat4
    const math::Matrix4x4& viewMat = camera->getViewMatrix();
    const math::Matrix4x4& projMat = camera->getProjectionMatrix();
    
    glm::mat4 view;
    glm::mat4 projection;
    
    // Copy matrix data (both are column-major)
    std::memcpy(&view[0][0], viewMat.data(), sizeof(float) * 16);
    std::memcpy(&projection[0][0], projMat.data(), sizeof(float) * 16);
    
    glm::mat4 inverseProjection = glm::inverse(projection);

    // TODO: Get command buffer from renderer
    // VkCommandBuffer cmd = ...;
    // clusteredPipelines_->execute(cmd, view, inverseProjection, 
    //                              getWidth(), getHeight(), getCurrentFrameIndex());
}

uint32_t ApplicationAdvanced::addClusteredLight(const glm::vec3& position,
                                                const glm::vec3& color,
                                                float radius,
                                                float intensity) {
    ClusteredLight light;
    light.position = glm::vec4(position, radius);
    light.color = glm::vec4(color, intensity);
    
    uint32_t index = static_cast<uint32_t>(clusteredLights_.size());
    clusteredLights_.push_back(light);

    // Update GPU buffer if clustered is initialized
    if (clusteredPipelines_) {
        clusteredPipelines_->setLights(clusteredLights_);
    }

    return index;
}

void ApplicationAdvanced::updateClusteredLightPosition(uint32_t index, const glm::vec3& position) {
    if (index >= clusteredLights_.size()) return;

    clusteredLights_[index].position = glm::vec4(position, clusteredLights_[index].position.w);

    if (clusteredPipelines_) {
        clusteredPipelines_->updateLightPosition(index, position);
    }
}

// ============================================================================
// Rendering
// ============================================================================

void ApplicationAdvanced::renderFromECS() {
    if (!ecsInitialized_) return;

    // Get render data from ECS
    auto snapshot = world_->acquireReadSnapshot();
    ecs::RenderBatch batch = ecs::extractRenderBatch(snapshot);

    if (!batch.valid() || batch.count == 0) return;

    // TODO: Integrate with existing PBR rendering pipeline
    // This would iterate over visible entities and issue draw calls
    // batch.forEachVisible([&](size_t idx, const glm::mat4& transform, 
    //                          uint32_t materialId, uint32_t meshId) {
    //     // Issue draw call for this entity
    // });
}

} // namespace jupiter::rendering

