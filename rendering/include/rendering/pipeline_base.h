#pragma once

/**
 * @file pipeline_base.h
 * @brief Base class for modular render/compute pipelines (HelloVulkan-inspired)
 * 
 * Provides a common interface for graphics, compute, and raytracing pipelines.
 * Each pipeline manages its own render pass, framebuffers, and descriptor sets.
 */

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace jupiter::rendering {

// Forward declarations
class VulkanContext;
struct CameraUBO;

/**
 * @brief Pipeline type classification
 */
enum class PipelineType {
    GraphicsOnScreen,   // Renders to swapchain
    GraphicsOffScreen,  // Renders to texture/framebuffer
    Compute,            // Compute shader dispatch
    Raytracing          // Hardware raytracing
};

/**
 * @brief Configuration for pipeline creation
 */
struct PipelineConfig {
    PipelineType type = PipelineType::GraphicsOnScreen;
    std::string name;
    
    // Shader files (SPIR-V)
    std::vector<std::string> shaderFiles;
    
    // Viewport/scissor (for graphics)
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    
    // Multisampling
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Render target format (for offscreen)
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    
    // Color attachment count (for MRT)
    uint32_t colorAttachmentCount = 1;
    
    // Enable depth test/write
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    
    // Cull mode
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    
    // Blend state
    bool blendEnable = false;
};

/**
 * @brief Base class for all render/compute pipelines
 * 
 * Encapsulates:
 * - Vulkan pipeline and layout
 * - Render pass (for graphics)
 * - Framebuffers
 * - Descriptor management
 * - Camera UBO (common to most pipelines)
 */
class PipelineBase {
public:
    explicit PipelineBase(VkDevice device, const PipelineConfig& config);
    virtual ~PipelineBase();

    // Non-copyable
    PipelineBase(const PipelineBase&) = delete;
    PipelineBase& operator=(const PipelineBase&) = delete;

    // ========================================================================
    // Core Interface (pure virtual)
    // ========================================================================

    /**
     * @brief Record commands into command buffer
     * 
     * Called each frame to record draw/dispatch commands.
     */
    virtual void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) = 0;

    /**
     * @brief Handle window resize
     * 
     * Called when swapchain is recreated.
     */
    virtual void onWindowResized(uint32_t width, uint32_t height);

    // ========================================================================
    // Optional Overrides
    // ========================================================================

    /**
     * @brief Update camera uniform buffer
     */
    virtual void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex);

    /**
     * @brief Called before rendering to prepare resources
     */
    virtual void prepareFrame(uint32_t frameIndex) {}

    /**
     * @brief Get pipeline type
     */
    PipelineType getType() const { return config_.type; }

    /**
     * @brief Get pipeline name
     */
    const std::string& getName() const { return config_.name; }

    /**
     * @brief Check if this is an offscreen pipeline
     */
    bool isOffscreen() const { 
        return config_.type == PipelineType::GraphicsOffScreen; 
    }

    /**
     * @brief Check if this is a compute pipeline
     */
    bool isCompute() const { 
        return config_.type == PipelineType::Compute; 
    }

protected:
    VkDevice device_ = VK_NULL_HANDLE;
    PipelineConfig config_;

    // Vulkan resources
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    
    // Descriptor management
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Camera UBOs (one per frame in flight)
    std::vector<VkBuffer> cameraUBOBuffers_;
    std::vector<VkDeviceMemory> cameraUBOMemory_;

    // Framebuffers (for offscreen rendering)
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkImageView> colorAttachments_;
    VkImageView depthAttachment_ = VK_NULL_HANDLE;

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * @brief Create pipeline layout from descriptor set layout
     */
    void createPipelineLayout(
        VkDescriptorSetLayout dsLayout,
        uint32_t pushConstantSize = 0,
        VkShaderStageFlags pushConstantStages = 0);

    /**
     * @brief Create graphics pipeline
     */
    void createGraphicsPipeline(
        VkRenderPass renderPass,
        const std::vector<std::string>& shaderFiles,
        const VkPipelineVertexInputStateCreateInfo* vertexInput = nullptr);

    /**
     * @brief Create compute pipeline
     */
    void createComputePipeline(const std::string& shaderFile);

    /**
     * @brief Load and create shader module from SPIR-V file
     */
    VkShaderModule loadShaderModule(const std::string& filename);

    /**
     * @brief Bind this pipeline in command buffer
     */
    void bindPipeline(VkCommandBuffer cmd);

    /**
     * @brief Begin render pass (for graphics pipelines)
     */
    void beginRenderPass(
        VkCommandBuffer cmd, 
        VkFramebuffer framebuffer,
        const VkClearValue* clearValues,
        uint32_t clearValueCount);

    /**
     * @brief End render pass
     */
    void endRenderPass(VkCommandBuffer cmd);

    /**
     * @brief Set viewport and scissor
     */
    void setViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height);
};

/**
 * @brief Camera uniform buffer structure
 */
struct CameraUBO {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
    alignas(16) glm::mat4 viewProjection;
    alignas(16) glm::vec4 cameraPosition;  // xyz = position, w = unused
    alignas(16) glm::vec4 nearFarFov;      // x = near, y = far, z = fov, w = aspect
};

} // namespace jupiter::rendering

