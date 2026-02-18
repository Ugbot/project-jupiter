#pragma once

/**
 * @file pipeline_smooth_terrain.h
 * @brief Rendering pipeline for smooth terrain (Marching Cubes / Transvoxel)
 *
 * Uses SmoothVertex format with position, normal, material, and AO.
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>
#include <cstdint>

namespace jupiter {
namespace rendering {

/**
 * @brief UBO for camera matrices (shared with voxel pipeline)
 */
struct SmoothTerrainCameraUBO {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 nearFarFov;
};

/**
 * @brief UBO for lighting
 */
struct SmoothTerrainLightUBO {
    glm::vec4 sunDirection;   // xyz = direction, w = intensity
    glm::vec4 sunColor;       // rgb = color
    glm::vec4 ambientColor;   // rgb = ambient
};

/**
 * @brief Push constant for per-chunk data
 */
struct SmoothTerrainPushConstant {
    glm::vec4 chunkOffset;    // xyz = world offset
    glm::vec4 scale;          // xyz = scale (usually 1,1,1)
};

/**
 * @brief Smooth terrain vertex for GPU
 *
 * Matches SmoothVertex layout (32 bytes)
 */
struct SmoothTerrainVertex {
    float posX, posY, posZ;   // 12 bytes: position
    float normX, normY, normZ; // 12 bytes: normal
    uint32_t packedData;      // 4 bytes: materialId(8) + ao(8) + texU(8) + texV(8)
    uint32_t padding;         // 4 bytes: padding
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(SmoothTerrainVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }
    
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        
        // Position (vec3)
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = 0;
        
        // Normal (vec3)
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = 12;
        
        // Packed data (uint)
        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32_UINT;
        attrs[2].offset = 24;
        
        return attrs;
    }
};

/**
 * @brief Rendering pipeline for smooth terrain
 */
class PipelineSmoothTerrain {
public:
    static constexpr uint32_t MAX_CHUNKS = 512;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    
    PipelineSmoothTerrain() = default;
    ~PipelineSmoothTerrain();
    
    // Non-copyable
    PipelineSmoothTerrain(const PipelineSmoothTerrain&) = delete;
    PipelineSmoothTerrain& operator=(const PipelineSmoothTerrain&) = delete;
    
    /**
     * @brief Initialize the pipeline
     */
    bool initialize(VkDevice device,
                   VkPhysicalDevice physicalDevice,
                   VmaAllocator allocator,
                   VkRenderPass renderPass,
                   VkFormat colorFormat,
                   VkFormat depthFormat);
    
    void destroy();
    
    /**
     * @brief Upload chunk mesh data
     * @param chunkIndex Chunk slot index
     * @param vertices Vertex data (SmoothVertex format)
     * @param vertexCount Number of vertices
     * @param indices Index data (can be null for non-indexed)
     * @param indexCount Number of indices
     * @param worldOffset Chunk world position
     * @return true on success
     */
    bool uploadChunkMesh(uint32_t chunkIndex,
                         const void* vertices,
                         uint32_t vertexCount,
                         const uint32_t* indices,
                         uint32_t indexCount,
                         const glm::vec3& worldOffset);
    
    /**
     * @brief Clear a chunk mesh
     */
    void clearChunkMesh(uint32_t chunkIndex);
    
    /**
     * @brief Set camera UBO for a frame
     */
    void setCameraUBO(const SmoothTerrainCameraUBO& ubo, uint32_t frameIndex);
    
    /**
     * @brief Set light UBO
     */
    void setLightUBO(const SmoothTerrainLightUBO& ubo);
    
    /**
     * @brief Record draw commands
     */
    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex);
    
    [[nodiscard]] bool isValid() const { return pipeline_ != VK_NULL_HANDLE; }
    
private:
    bool createDescriptorSetLayout();
    bool createPipelineLayout();
    bool createPipeline(VkRenderPass renderPass, VkFormat colorFormat, VkFormat depthFormat);
    bool createDescriptorPool();
    bool createUniformBuffers();
    bool createDescriptorSets();
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets_{};
    
    // Per-frame uniform buffers
    struct FrameUBO {
        VkBuffer cameraBuffer = VK_NULL_HANDLE;
        VmaAllocation cameraAlloc = VK_NULL_HANDLE;
        void* cameraMapped = nullptr;
        
        VkBuffer lightBuffer = VK_NULL_HANDLE;
        VmaAllocation lightAlloc = VK_NULL_HANDLE;
        void* lightMapped = nullptr;
    };
    std::array<FrameUBO, MAX_FRAMES_IN_FLIGHT> frameUBOs_{};
    
    // Chunk buffers
    struct ChunkBuffer {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAlloc = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAlloc = VK_NULL_HANDLE;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        glm::vec3 worldOffset{0.0f};
        bool valid = false;
    };
    std::array<ChunkBuffer, MAX_CHUNKS> chunkBuffers_{};
};

} // namespace rendering
} // namespace jupiter



