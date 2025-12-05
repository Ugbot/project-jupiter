#pragma once

/**
 * @file pipeline_gbuffer.h
 * @brief G-buffer geometry pass pipeline
 * 
 * Renders scene geometry to output view-space position and normals
 * for subsequent SSAO calculation.
 */

#include "pipeline_base.h"
#include "resources_gbuffer.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

// Forward declarations
class SceneManager;

/**
 * @brief Push constants for G-buffer pass
 */
struct GBufferPushConstants {
    alignas(16) glm::mat4 model;       // Model matrix
    alignas(16) glm::mat4 normalMatrix; // Inverse transpose of model-view
};

/**
 * @brief G-buffer geometry pass pipeline
 * 
 * Outputs:
 * - Position texture: view-space XYZ + linear depth
 * - Normal texture: view-space normals
 */
class PipelineGBuffer : public PipelineBase {
public:
    /**
     * @brief Create G-buffer pipeline
     */
    PipelineGBuffer(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    const PipelineConfig& config,
                    ResourcesGBuffer* resourcesGBuffer);

    ~PipelineGBuffer() override;

    /**
     * @brief Record G-buffer pass commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height) override;

    /**
     * @brief Update camera UBO for view-space calculations
     */
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override;

    /**
     * @brief Set scene manager for renderable access
     */
    void setSceneManager(SceneManager* scene) { sceneManager_ = scene; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUBOBuffers();
    void createPipeline();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ResourcesGBuffer* resourcesGBuffer_ = nullptr;
    SceneManager* sceneManager_ = nullptr;

    std::vector<VkDescriptorSet> gBufferDescriptorSets_;
    std::vector<GPUBuffer> cameraUBOs_;
    
    CameraUBO currentCameraUBO_;
};

} // namespace jupiter::rendering

