#pragma once

/**
 * @file pipeline_gbuffer.h
 * @brief G-buffer geometry pass pipeline for deferred rendering
 * 
 * Renders scene geometry to output:
 * - Position (view-space XYZ + linear depth)
 * - Normals (view-space, normal mapped)
 * - Albedo + Metallic
 * - Roughness + AO
 * - Emissive
 */

#include "pipeline_base.h"
#include "resources_gbuffer.h"
#include "material_system.h"
#include <glm/glm.hpp>

namespace jupiter::rendering {

// Forward declarations
class SceneManager;

/**
 * @brief Push constants for G-buffer pass (standard geometry)
 */
struct GBufferPushConstants {
    alignas(16) glm::mat4 model;       // Model matrix
    alignas(16) glm::mat4 normalMatrix; // Inverse transpose of model-view
};

/**
 * @brief Push constants for voxel G-buffer pass
 *
 * Voxel chunks use offset + scale instead of full model matrix.
 * Matches VoxelChunkPushConstant in gbuffer_voxel.vert shader.
 */
struct VoxelGBufferPushConstants {
    alignas(16) glm::vec4 chunkOffset;  // xyz = world offset, w = unused
    alignas(16) glm::vec4 scale;        // xyz = stb scale, w = unused
};

/**
 * @brief G-buffer geometry pass pipeline for deferred rendering
 * 
 * Outputs (5 color attachments + depth):
 * - Position texture: view-space XYZ + linear depth (W)
 * - Normal texture: view-space normals (RGB)
 * - Albedo texture: albedo RGB + metallic (A)
 * - Material texture: roughness (R) + AO (G)
 * - Emissive texture: emissive RGB
 */
class PipelineGBuffer : public PipelineBase {
public:
    /**
     * @brief Create G-buffer pipeline
     */
    PipelineGBuffer(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    const PipelineConfig& config,
                    ResourcesGBuffer* resourcesGBuffer,
                    MaterialSystem* materialSystem = nullptr);

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

    /**
     * @brief Set material system for descriptor set layout
     */
    void setMaterialSystem(MaterialSystem* materialSystem) { materialSystem_ = materialSystem; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUBOBuffers();
    void createPipeline();
    void createVoxelPipeline();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    ResourcesGBuffer* resourcesGBuffer_ = nullptr;
    SceneManager* sceneManager_ = nullptr;
    MaterialSystem* materialSystem_ = nullptr;

    std::vector<VkDescriptorSet> gBufferDescriptorSets_;
    std::vector<GPUBuffer> cameraUBOs_;

    CameraUBO currentCameraUBO_;

    // Voxel pipeline (uses VoxelVertexGPU format + VoxelGBufferPushConstants)
    VkPipeline voxelPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout voxelPipelineLayout_ = VK_NULL_HANDLE;
};

} // namespace jupiter::rendering

