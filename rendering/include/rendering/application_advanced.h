#pragma once

/**
 * @file application_advanced.h
 * @brief Extended Application class with ECS and advanced rendering integration
 * 
 * Extends the base Application with:
 * - ECS World integration (double-buffered, multi-reader)
 * - Clustered forward shading
 * - GPU-driven rendering
 * - Feature toggles
 */

#include "application.h"
#include "application_features.h"
#include "render_features.h"
#include "resources_clustered.h"
#include "pipeline_clustered.h"
#include "pipeline_base.h"
#include "resources_base.h"
#include "ecs/ecs.h"

#include <memory>
#include <vector>
#include <functional>

namespace jupiter::rendering {

/**
 * @brief Extended Application with ECS and advanced rendering
 * 
 * This class provides:
 * - Integrated ECS World with automatic frame sync
 * - Clustered forward shading for many lights
 * - Runtime feature toggles
 * - ECS-driven rendering pipeline
 * 
 * Frame Loop:
 * 1. onUpdate(): Game logic, ECS write operations
 * 2. ECS kernels execute (physics, transforms)
 * 3. ECS world swap (readers see new data)
 * 4. Compute passes (AABB gen, light culling)
 * 5. onRender(): Custom rendering + auto-render
 */
class RENDERING_API ApplicationAdvanced : public Application {
public:
    /**
     * @brief Construct an advanced application
     */
    ApplicationAdvanced(const std::string& title, uint32_t width, uint32_t height,
                       bool enableValidation = false);

    virtual ~ApplicationAdvanced();

protected:
    // ========================================================================
    // ECS Integration
    // ========================================================================

    /**
     * @brief Get the ECS World
     * 
     * Use for entity creation, destruction, and component updates.
     * In onUpdate(), you have write access. In onRender(), use snapshots.
     */
    ecs::World& getWorld() { return *world_; }
    const ecs::World& getWorld() const { return *world_; }

    /**
     * @brief Create an entity with common components
     * 
     * Convenience function for creating renderable entities.
     * 
     * @param position World position
     * @param meshId Mesh to render
     * @param materialId Material to use
     * @return Entity index
     */
    uint32_t createEntity(const glm::vec3& position,
                         uint32_t meshId = 0,
                         uint32_t materialId = 0);

    /**
     * @brief Create a physics entity
     * 
     * Entity with physics simulation enabled.
     */
    uint32_t createPhysicsEntity(const glm::vec3& position,
                                float mass,
                                uint32_t meshId = 0,
                                uint32_t materialId = 0);

    /**
     * @brief Get a read snapshot for rendering
     * 
     * Thread-safe access to ECS data for rendering.
     * Valid until next world swap.
     */
    ecs::ReadSnapshot acquireRenderSnapshot() {
        return world_->acquireReadSnapshot();
    }

    /**
     * @brief Execute a kernel by name
     */
    ecs::Status executeKernel(const char* name) {
        return ecs::KernelRegistry::instance().execute(name, *world_, kernelContext_);
    }

    // ========================================================================
    // Render Features
    // ========================================================================

    /**
     * @brief Get render features configuration
     */
    RenderFeatures& getFeatures() { return appFeatures_.features(); }
    const RenderFeatures& getFeatures() const { return appFeatures_.features(); }

    /**
     * @brief Get application features manager (for advanced control)
     */
    ApplicationFeatures& getAppFeatures() { return appFeatures_; }

    /**
     * @brief Enable a render feature
     */
    void enableFeature(RenderFeature feature) {
        appFeatures_.enableFeature(feature);
        onFeaturesChanged();
    }

    /**
     * @brief Disable a render feature
     */
    void disableFeature(RenderFeature feature) {
        appFeatures_.disableFeature(feature);
        onFeaturesChanged();
    }

    /**
     * @brief Toggle a render feature
     */
    void toggleFeature(RenderFeature feature) {
        if (appFeatures_.features().isEnabled(feature)) {
            appFeatures_.disableFeature(feature);
        } else {
            appFeatures_.enableFeature(feature);
        }
        onFeaturesChanged();
    }

    /**
     * @brief Check if feature is enabled
     */
    bool isFeatureEnabled(RenderFeature feature) const {
        return appFeatures_.features().isEnabled(feature);
    }

    /**
     * @brief Check if feature is actually active (resources created)
     */
    bool isFeatureActive(RenderFeature feature) const {
        return appFeatures_.isFeatureActive(feature);
    }

    /**
     * @brief Update shadow light parameters for shadow mapping
     */
    void updateShadowLight(const glm::vec3& lightPos,
                          const glm::vec3& lightDir,
                          const glm::vec3& targetPos) {
        appFeatures_.updateShadowLight(lightPos, lightDir, targetPos);
    }

    // ========================================================================
    // Clustered Forward Shading
    // ========================================================================

    /**
     * @brief Add a clustered light
     * 
     * @param position World position
     * @param color RGB color
     * @param radius Light range
     * @param intensity Light intensity
     * @return Light index
     */
    uint32_t addClusteredLight(const glm::vec3& position,
                               const glm::vec3& color,
                               float radius,
                               float intensity = 1.0f);

    /**
     * @brief Update a clustered light's position
     */
    void updateClusteredLightPosition(uint32_t index, const glm::vec3& position);

    /**
     * @brief Get clustered forward resources (for custom binding)
     */
    ResourcesClusteredForward* getClusteredResources() {
        return clusteredPipelines_ ? &clusteredPipelines_->getClusteredResources() : nullptr;
    }

    /**
     * @brief Get light resources (for custom binding)
     */
    ResourcesLight* getLightResources() {
        return clusteredPipelines_ ? &clusteredPipelines_->getLightResources() : nullptr;
    }

    // ========================================================================
    // Pipeline Management
    // ========================================================================

    /**
     * @brief Register a custom pipeline
     * 
     * @tparam T Pipeline type (must derive from PipelineBase)
     * @param args Constructor arguments
     * @return Pointer to created pipeline
     */
    template<typename T, typename... Args>
    T* registerPipeline(Args&&... args) {
        static_assert(std::is_base_of_v<PipelineBase, T>, 
                      "T must derive from PipelineBase");
        auto pipeline = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = pipeline.get();
        pipelines_.push_back(std::move(pipeline));
        return ptr;
    }

    /**
     * @brief Register custom resources
     * 
     * @tparam T Resources type (must derive from ResourcesBase)
     * @param args Constructor arguments
     * @return Pointer to created resources
     */
    template<typename T, typename... Args>
    T* registerResources(Args&&... args) {
        static_assert(std::is_base_of_v<ResourcesBase, T>,
                      "T must derive from ResourcesBase");
        auto resources = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = resources.get();
        resources_.push_back(std::move(resources));
        return ptr;
    }

    // ========================================================================
    // Lifecycle Hooks (override these)
    // ========================================================================

    /**
     * @brief Called when render features change
     * 
     * Override to recreate pipelines or resources when features toggle.
     */
    virtual void onFeaturesChanged() {}

    /**
     * @brief Called after ECS kernels execute, before rendering
     * 
     * Good place for custom GPU work that depends on updated ECS data.
     */
    virtual void onPostKernels() {}

protected:
    // Base class overrides (call base implementation)
    void onInit() override;
    void onUpdate(float deltaTime) override;
    void onRender() override;
    void onShutdown() override;
    void onPreRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void onPostRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) override;

    // Called by onInit - override to add custom initialization
    virtual void onInitAdvanced() {}

    // Called by onRender - override for custom rendering
    virtual void onRenderAdvanced() {}

    // Called by onShutdown - override for custom cleanup
    virtual void onShutdownAdvanced() {}

private:
    // ECS
    std::unique_ptr<ecs::World> world_;
    ecs::KernelContext kernelContext_;

    // Advanced rendering features (shadow, SSAO, HDR, skybox)
    ApplicationFeatures appFeatures_;

    // Clustered forward
    std::unique_ptr<ClusteredForwardPipelines> clusteredPipelines_;
    std::vector<ClusteredLight> clusteredLights_;

    // Custom pipelines and resources
    std::vector<std::unique_ptr<PipelineBase>> pipelines_;
    std::vector<std::unique_ptr<ResourcesBase>> resources_;

    // State
    bool ecsInitialized_ = false;
    bool clusteredInitialized_ = false;
    bool featuresInitialized_ = false;

    // Internal methods
    void initializeECS();
    void initializeClusteredForward();
    void initializeAdvancedFeatures();
    void executeECSKernels(float deltaTime);
    void executeClusteredPasses();
    void executeAdvancedPasses(VkCommandBuffer cmd, uint32_t frameIndex);
    void renderFromECS();
};

} // namespace jupiter::rendering

