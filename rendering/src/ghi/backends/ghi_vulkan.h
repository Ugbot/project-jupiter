#pragma once

/**
 * @file ghi_vulkan.h
 * @brief GHI Vulkan Backend Implementation
 * 
 * Implements IGHIBackend interface using Vulkan API.
 * Wraps existing Jupiter Vulkan infrastructure.
 */

#include "rendering/ghi/ighi_backend.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <unordered_map>
#include <memory>

// Forward declarations
namespace jupiter {
namespace rendering {
namespace vulkan {
    class VulkanRenderer;
    class VulkanBuffer;
}
}
}

namespace jupiter {
namespace rendering {
namespace ghi {

/**
 * @brief Vulkan backend implementation
 * 
 * Wraps Jupiter's existing Vulkan renderer into GHI interface.
 * Primary backend for Linux/Windows.
 * Also works via MoltenVK on macOS (for Vulkan testing).
 */
class GHI_VulkanBackend : public IGHIBackend {
public:
    GHI_VulkanBackend();
    ~GHI_VulkanBackend() override;

    // IGHIBackend interface
    bool initialize() override;
    void shutdown() override;
    void waitIdle() override;

    // Resource management
    BufferHandle createBuffer(const BufferCreateInfo& info) override;
    void destroyBuffer(BufferHandle handle) override;
    void updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) override;

    TextureHandle createTexture(const TextureCreateInfo& info) override;
    void destroyTexture(TextureHandle handle) override;
    void updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, const void* data) override;

    ShaderHandle createShader(const ShaderSource& source) override;
    void destroyShader(ShaderHandle handle) override;
    
    SamplerHandle createSampler(const SamplerCreateInfo& info) override;
    void destroySampler(SamplerHandle handle) override;
    void bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) override;

    // Command recording
    void beginFrame() override;
    void endFrame() override;
    void beginRenderPass() override;
    void beginRenderPass(RenderTargetHandle target) override;
    void endRenderPass() override;

    void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    
    // Render targets
    RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo& info) override;
    void destroyRenderTarget(RenderTargetHandle handle) override;
    TextureHandle getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) override;
    TextureHandle getRenderTargetDepthTexture(RenderTargetHandle target) override;
    void resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) override;

    // Drawing
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, 
                    int32_t vertexOffset, uint32_t firstInstance) override;
    void drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) override;
    void drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) override;

    // Compute
    ShaderHandle createComputeShader(const ShaderSource& source) override;
    void bindComputeShader(ShaderHandle shader) override;
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void dispatchIndirect(BufferHandle indirectBuffer) override;

    // State management
    void setRenderState(const RenderState& state) override;
    void getRenderState(RenderState& state) override;

    void bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) override;
    void bindIndexBuffer(BufferHandle buffer, size_t offset) override;
    void bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) override;
    void bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) override;
    void bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) override;
    void bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) override;
    void setPushConstants(const void* data, uint32_t size, uint32_t offset = 0) override;

    // Synchronization
    void memoryBarrier() override;
    void bufferBarrier(BufferHandle buffer) override;
    void textureBarrier(TextureHandle texture) override;

    // Queries
    const Capabilities& getCapabilities() const override { return capabilities_; }
    Backend getBackendType() const override { return Backend::Vulkan; }
    
    // Vulkan-specific: Set surface from SDL window
    VkInstance getInstance() const { return instance_; }
    void setSurface(VkSurfaceKHR surface, uint32_t width, uint32_t height);
    bool createSwapchain(uint32_t width, uint32_t height);

private:
    // Vulkan handles (standalone - not wrapper)
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    
    // Swapchain (for windowed rendering)
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat swapchainImageFormat_;
    VkExtent2D swapchainExtent_;
    uint32_t currentImageIndex_ = 0;
    
    // Command buffers
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkCommandBuffer currentCommandBuffer_ = VK_NULL_HANDLE;
    uint32_t currentFrame_ = 0;
    
    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    
    // Render pass (simple for now)
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    
    // Depth buffer
    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;

    // Resource tracking
    uint32_t nextBufferID_ = 1;
    uint32_t nextTextureID_ = 1;
    uint32_t nextShaderID_ = 1;
    uint32_t nextRenderTargetID_ = 1;
    uint32_t nextSamplerID_ = 1;
    
    // Standalone samplers (separate from texture-bound samplers)
    std::unordered_map<uint32_t, VkSampler> standaloneSamplers_;

    std::unordered_map<uint32_t, VkBuffer> buffers_;
    std::unordered_map<uint32_t, VmaAllocation> bufferAllocations_;
    std::unordered_map<uint32_t, VkImage> images_;
    std::unordered_map<uint32_t, VmaAllocation> imageAllocations_;
    std::unordered_map<uint32_t, VkImageView> imageViews_;
    std::unordered_map<uint32_t, VkSampler> samplers_;
    std::unordered_map<uint32_t, VkPipelineLayout> pipelineLayouts_;
    std::unordered_map<uint32_t, VkPipeline> computePipelines_;
    
    // Shader modules for pipeline recreation
    struct VulkanShaderData {
        VkShaderModule vertModule = VK_NULL_HANDLE;
        VkShaderModule fragModule = VK_NULL_HANDLE;
    };
    std::unordered_map<uint32_t, VulkanShaderData> shaderModules_;

    // Pipeline cache key
    struct PipelineKey {
        uint32_t shaderId;
        bool blendEnabled;
        BlendFactor srcColorBlendFactor;
        BlendFactor dstColorBlendFactor;
        BlendOp colorBlendOp;
        BlendFactor srcAlphaBlendFactor;
        BlendFactor dstAlphaBlendFactor;
        BlendOp alphaBlendOp;

        bool operator==(const PipelineKey& other) const {
            return shaderId == other.shaderId &&
                   blendEnabled == other.blendEnabled &&
                   srcColorBlendFactor == other.srcColorBlendFactor &&
                   dstColorBlendFactor == other.dstColorBlendFactor &&
                   colorBlendOp == other.colorBlendOp &&
                   srcAlphaBlendFactor == other.srcAlphaBlendFactor &&
                   dstAlphaBlendFactor == other.dstAlphaBlendFactor &&
                   alphaBlendOp == other.alphaBlendOp;
        }
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& k) const {
            size_t h = std::hash<uint32_t>{}(k.shaderId);
            auto hash_combine = [&](size_t val) {
                h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
            };
            hash_combine(std::hash<bool>{}(k.blendEnabled));
            hash_combine(static_cast<size_t>(k.srcColorBlendFactor));
            hash_combine(static_cast<size_t>(k.dstColorBlendFactor));
            hash_combine(static_cast<size_t>(k.colorBlendOp));
            hash_combine(static_cast<size_t>(k.srcAlphaBlendFactor));
            hash_combine(static_cast<size_t>(k.dstAlphaBlendFactor));
            hash_combine(static_cast<size_t>(k.alphaBlendOp));
            return h;
        }
    };
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelineCache_;
    
    // Render target data structure
    struct RenderTargetData {
        uint32_t width = 0;
        uint32_t height = 0;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        std::vector<TextureHandle> colorTextures;
        TextureHandle depthTexture;
        std::vector<VkClearValue> clearValues;
    };
    std::unordered_map<uint32_t, RenderTargetData> renderTargets_;
    RenderTargetHandle currentRenderTarget_;  // Currently active render target (0 = swapchain)
    
    // Descriptor sets (for uniform binding)
    VkDescriptorSetLayout descriptorSetLayout0_ = VK_NULL_HANDLE;  // Camera + object + texture
    VkDescriptorSetLayout descriptorSetLayout1_ = VK_NULL_HANDLE;  // Lighting + material
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;  // Per-frame descriptor sets

    Capabilities capabilities_;
    RenderState currentState_;
    
    // Queue family indices
    struct QueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        uint32_t computeFamily = UINT32_MAX;
        bool isComplete() const {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };
    QueueFamilyIndices queueFamilies_;

    // Helpers
    void queryCapabilities();
    VkFormat convertFormat(Format format);
    
    // Initialization helpers (standalone)
    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createCommandPool();
    bool createSyncObjects();
    bool createRenderPass();
    bool createFramebuffers();
    bool createDepthResources();
    
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    
    // Shader helpers
    bool loadSPIRV(const char* filename, std::vector<uint32_t>& code);
    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
    VkPipeline createGraphicsPipeline(VkShaderModule vertModule, VkShaderModule fragModule, const RenderState& state, uint32_t shaderId);
    bool createDescriptorSetLayouts();
    bool createDescriptorPool();
    bool allocateDescriptorSets();
};

} // namespace ghi
} // namespace rendering
} // namespace jupiter

