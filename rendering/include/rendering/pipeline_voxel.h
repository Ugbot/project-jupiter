#pragma once

/**
 * @file pipeline_voxel.h
 * @brief Voxel rendering pipeline for stb_voxel_render output
 *
 * Renders voxel chunks using the 8-byte VoxelVertexGPU format
 * directly from stb_voxel_render Mode 30 output.
 */

#include "pipeline_base.h"
#include "vertex_formats.h"
#include <glm/glm.hpp>
#include <vector>

namespace jupiter::rendering {

// Forward declaration
class ResourcesShadow;

/**
 * @brief Push constants for per-chunk rendering
 */
struct VoxelPushConstants {
    alignas(16) glm::vec4 chunkOffset;  // xyz = world offset, w = unused
    alignas(16) glm::vec4 scale;        // xyz = stb transform scale, w = unused
};

// VoxelShadowPushConstants is defined in pipeline_shadow.h (same layout as VoxelPushConstants)

/**
 * @brief GPU buffer for a chunk mesh
 *
 * stb_voxel_render outputs quads (4 vertices each), so we need
 * an index buffer to convert to triangles for rendering.
 */
struct VoxelChunkBuffer {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;  // Number of indices (6 per quad)
    glm::vec3 worldOffset{0.0f};
    glm::vec3 scale{1.0f};
    bool valid = false;
};

/**
 * @brief Light UBO for simple voxel lighting
 */
struct VoxelLightUBO {
    alignas(16) glm::vec4 sunDirection;   // xyz = direction, w = intensity
    alignas(16) glm::vec4 sunColor;       // rgb = color, a = unused
    alignas(16) glm::vec4 ambientColor;   // rgb = ambient, a = unused
};

/**
 * @brief Simple voxel rendering pipeline
 *
 * Renders voxel chunks with basic directional lighting and
 * ambient occlusion from stb_voxel_render output.
 */
class PipelineVoxel : public PipelineBase {
public:
    /**
     * @brief Create voxel pipeline
     * @param device Vulkan device
     * @param physicalDevice Physical device (for memory allocation)
     * @param config Pipeline configuration
     * @param renderPass Render pass to use (typically swapchain render pass)
     * @param colorFormat Color attachment format
     * @param depthFormat Depth attachment format
     */
    PipelineVoxel(VkDevice device,
                  VkPhysicalDevice physicalDevice,
                  const PipelineConfig& config,
                  VkRenderPass renderPass,
                  VkFormat colorFormat,
                  VkFormat depthFormat);

    ~PipelineVoxel() override;

    /**
     * @brief Record voxel rendering commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;

    /**
     * @brief Handle window resize
     */
    void onWindowResized(uint32_t width, uint32_t height) override;

    /**
     * @brief Update camera UBO
     */
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override;

    /**
     * @brief Set light parameters
     */
    void setLightUBO(const VoxelLightUBO& light);

    /**
     * @brief Enable shadow mapping
     * @param shadowResources Pointer to shadow resources (must remain valid)
     *
     * This recreates the pipeline with shadow shader and descriptor bindings.
     * Must be called after construction but before rendering.
     */
    void enableShadowMapping(ResourcesShadow* shadowResources);

    /**
     * @brief Update shadow UBO for current frame
     * @param frameIndex Current frame index
     */
    void updateShadowUBO(uint32_t frameIndex);

    /**
     * @brief Check if shadow mapping is enabled
     */
    bool isShadowMappingEnabled() const { return shadowEnabled_; }

    /**
     * @brief Render all chunks to shadow map (depth pass from light's perspective)
     *
     * This must be called BEFORE the main render pass to populate the shadow map.
     * Renders using depth_voxel.vert shader with VoxelShadowPushConstants.
     *
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     */
    void fillShadowDepthBuffer(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @brief Upload chunk mesh to GPU
     * @param chunkIndex Slot index for this chunk
     * @param vertices Raw vertex data from stb_voxel_render
     * @param numBytes Size of vertex data in bytes
     * @param worldOffset World position of chunk origin
     * @param scale Scale from stb transform
     * @return true if upload successful
     */
    bool uploadChunkMesh(uint32_t chunkIndex,
                         const void* vertices,
                         size_t numBytes,
                         const glm::vec3& worldOffset,
                         const glm::vec3& scale);

    /**
     * @brief Clear chunk mesh (mark slot as unused)
     */
    void clearChunkMesh(uint32_t chunkIndex);

    /**
     * @brief Get number of active chunks
     */
    uint32_t getActiveChunkCount() const;

    /**
     * @brief Maximum number of chunk slots
     */
    // NOTE: This pipeline uses one vertex/index buffer per chunk slot.
    // Keep this large enough for the demo view distance, but be mindful that
    // many valid chunks means many draw calls.
    static constexpr uint32_t MAX_CHUNKS = 4096;

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createUBOBuffers();
    void createPipeline();

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkRenderPass externalRenderPass_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

    // Descriptor sets for camera and light UBOs
    std::vector<VkDescriptorSet> voxelDescriptorSets_;

    // Camera UBOs (one per frame in flight)
    struct GPUBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mappedData = nullptr;
        VkDeviceSize size = 0;

        VkDescriptorBufferInfo getDescriptorInfo() const {
            return {buffer, 0, size};
        }
    };

    std::vector<GPUBuffer> cameraUBOs_;
    std::vector<GPUBuffer> lightUBOs_;

    // Current light settings
    VoxelLightUBO currentLight_;

    // Chunk mesh buffers
    std::vector<VoxelChunkBuffer> chunkBuffers_;

    // Current camera for frustum culling
    CameraUBO currentCamera_;

    // Shadow mapping support
    bool shadowEnabled_ = false;
    ResourcesShadow* shadowResources_ = nullptr;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;  // Shadow-enabled pipeline
    VkDescriptorSetLayout shadowDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool shadowDescriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> shadowDescriptorSets_;

    // Shadow depth pass (renders to shadow map from light's perspective)
    VkPipeline depthPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout depthPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout depthDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool depthDescriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> depthDescriptorSets_;

    // Deferred deletion queue - buffers pending destruction after GPU is done
    struct PendingDeletion {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint64_t frameQueued = 0;  // Global frame counter when queued for deletion
    };
    std::vector<PendingDeletion> pendingDeletions_;
    uint32_t currentFrameIndex_ = 0;       // Vulkan frame index (0 or 1)
    uint64_t globalFrameCounter_ = 0;       // Monotonically increasing frame counter
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

    void processDeletionQueue();
};

} // namespace jupiter::rendering
