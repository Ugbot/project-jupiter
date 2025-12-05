#pragma once

#include <vulkan/vulkan.h>

// Define VMA implementation in vulkan_backend.cpp
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <vector>
#include <string>
#include <memory>
#include <array>

// Include vertex format system for pipeline configuration
#include "rendering/vertex_formats.h"

// Forward declaration for Window
namespace jupiter {
namespace rendering {
    class Window;
}
}

namespace jupiter {
namespace rendering {
namespace vulkan {

// Maximum frames that can be processed concurrently
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// ============================================================================
// VulkanBuffer - Manages Vulkan buffers and memory (using VMA)
// ============================================================================
class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() { destroy(); }

    // No copy, allow move
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    // VMA-based creation
    bool create(VmaAllocator allocator, VkDeviceSize size,
                VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                VmaAllocationCreateFlags flags = 0);

    // Create buffer with device address support (for bindless/raytracing)
    bool createWithDeviceAddress(VkDevice device, VmaAllocator allocator,
                                 VkDeviceSize size, VkBufferUsageFlags usage,
                                 VmaMemoryUsage memoryUsage,
                                 VmaAllocationCreateFlags flags = 0);

    // Legacy creation (for backward compatibility during transition)
    bool create(VkDevice device, VkPhysicalDevice physicalDevice,
                VkDeviceSize size, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties);

    void destroy();

    // Upload data to buffer
    bool upload(const void* data, VkDeviceSize size);

    VkBuffer getBuffer() const { return buffer_; }
    VkDeviceMemory getMemory() const { return memory_; }
    VkDeviceSize getSize() const { return size_; }

    // Buffer Device Address (BDA) support for bindless rendering
    VkDeviceAddress getDeviceAddress() const { return deviceAddress_; }
    bool hasDeviceAddress() const { return deviceAddress_ != 0; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags properties_ = 0;
    bool usesVMA_ = false;
    VkDeviceAddress deviceAddress_ = 0;  // For bindless/raytracing (BDA)

    uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                           uint32_t typeFilter,
                           VkMemoryPropertyFlags properties);
};

// ============================================================================
// VulkanSwapchain - Manages the swapchain
// ============================================================================
class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain() { destroy(); }

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    bool create(VkDevice device, VkPhysicalDevice physicalDevice,
                VkSurfaceKHR surface, uint32_t width, uint32_t height,
                VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);

    void destroy();

    VkSwapchainKHR getSwapchain() const { return swapchain_; }
    VkFormat getFormat() const { return format_; }
    VkExtent2D getExtent() const { return extent_; }
    const std::vector<VkImage>& getImages() const { return images_; }
    const std::vector<VkImageView>& getImageViews() const { return imageViews_; }
    uint32_t getImageCount() const { return static_cast<uint32_t>(images_.size()); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                uint32_t width, uint32_t height);
};

// ============================================================================
// VulkanPipeline - Manages graphics pipeline and shaders
// ============================================================================
class VulkanPipeline {
public:
    /**
     * @brief Configuration for pipeline creation
     */
    struct Config {
        // Vertex input (can be nullptr for no vertex input)
        const VertexInputDescription* vertexInput = nullptr;

        // Descriptor set layouts (can be empty)
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

        // Push constants (can be empty)
        std::vector<VkPushConstantRange> pushConstants;

        // Rasterization state
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;

        // Depth/stencil (for future use)
        bool depthTestEnable = false;
        bool depthWriteEnable = false;

        // Blend state
        bool blendEnable = false;
    };

    VulkanPipeline() = default;
    ~VulkanPipeline() { destroy(); }

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    /**
     * @brief Create pipeline with flexible configuration
     * @param device Vulkan device
     * @param renderPass Render pass
     * @param extent Viewport extent
     * @param vertShaderPath Vertex shader path
     * @param fragShaderPath Fragment shader path
     * @param config Pipeline configuration
     * @return true if successful
     */
    bool create(VkDevice device, VkRenderPass renderPass,
                VkExtent2D extent, const std::string& vertShaderPath,
                const std::string& fragShaderPath, const Config& config);

    /**
     * @brief Legacy create for backward compatibility (2D vertex, optional single UBO)
     */
    bool create(VkDevice device, VkRenderPass renderPass,
                VkExtent2D extent, const std::string& vertShaderPath,
                const std::string& fragShaderPath, bool useDescriptors = false);

    void destroy();

    /**
     * @brief Reload pipeline with new shader code (for hot reload)
     * @param vertCode New vertex shader SPIR-V
     * @param fragCode New fragment shader SPIR-V
     * @return true if reload successful
     */
    bool reload(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);

    /**
     * @brief Get shader paths for tracking
     */
    const std::string& getVertexShaderPath() const { return vertShaderPath_; }
    const std::string& getFragmentShaderPath() const { return fragShaderPath_; }

    VkPipeline getPipeline() const { return pipeline_; }
    VkPipelineLayout getLayout() const { return layout_; }
    const std::vector<VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return descriptorSetLayouts_; }
    bool hasDescriptors() const { return !descriptorSetLayouts_.empty(); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkExtent2D extent_ = {0, 0};
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts_; // Can have multiple sets
    VkShaderModule vertShaderModule_ = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule_ = VK_NULL_HANDLE;

    // Store configuration for recreating pipeline
    Config config_;

    // Shader paths for hot reload tracking
    std::string vertShaderPath_;
    std::string fragShaderPath_;

    // Track if we own the descriptor set layouts (legacy path creates them)
    bool ownsDescriptorSetLayouts_ = false;

    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
    bool loadShaderFile(const std::string& filename, std::vector<uint32_t>& code);
    bool recreatePipeline(VkShaderModule vertModule, VkShaderModule fragModule);
};

// ============================================================================
// VulkanContext - Core Vulkan initialization and device management
// ============================================================================
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() { destroy(); }

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool initialize(const Window& window, bool enableValidation);
    void destroy();

    VkInstance getInstance() const { return instance_; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice_; }
    VkDevice getDevice() const { return device_; }
    VkQueue getGraphicsQueue() const { return graphicsQueue_; }
    VkQueue getPresentQueue() const { return presentQueue_; }
    VkSurfaceKHR getSurface() const { return surface_; }
    VmaAllocator getAllocator() const { return allocator_; }
    uint32_t getGraphicsFamily() const { return graphicsFamily_; }
    uint32_t getPresentFamily() const { return presentFamily_; }

    bool isValid() const { return device_ != VK_NULL_HANDLE; }

    // Feature availability checks (for bindless/raytracing support)
    bool hasBufferDeviceAddress() const { return hasBufferDeviceAddress_; }
    bool hasDescriptorIndexing() const { return hasDescriptorIndexing_; }
    bool hasSynchronization2() const { return hasSynchronization2_; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    uint32_t graphicsFamily_ = UINT32_MAX;
    uint32_t presentFamily_ = UINT32_MAX;

    bool enableValidation_ = false;

    // Track available Vulkan features for conditional VMA configuration
    bool hasBufferDeviceAddress_ = false;
    bool hasDescriptorIndexing_ = false;
    bool hasSynchronization2_ = false;

    bool createInstance(const Window& window);
    bool setupDebugMessenger();
    bool createSurface(const Window& window);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool findQueueFamilies(VkPhysicalDevice device);
};

// ============================================================================
// VulkanRenderer - High-level rendering interface
// ============================================================================
class VulkanRenderer {
public:
    struct Vertex {
        float pos[2];
        float color[3];
    };

    struct Vertex3D {
        float pos[3];
        float color[3];
    };

    // Uniform buffer object for MVP matrices
    struct UniformBufferObject {
        alignas(16) float model[16];
        alignas(16) float view[16];
        alignas(16) float projection[16];
    };

    VulkanRenderer() = default;
    ~VulkanRenderer() { destroy(); }

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(const Window& window, bool enableValidation);
    void destroy();
    void waitIdle();

    // Frame rendering
    bool beginFrame(uint32_t& imageIndex);
    void endFrame(uint32_t imageIndex);
    VkCommandBuffer getCurrentCommandBuffer() const {
        return commandBuffers_[currentFrame_];
    }

    // Drawing commands
    void beginRenderPass(uint32_t imageIndex);
    void endRenderPass();
    void bindPipeline();
    void drawIndexed(const VulkanBuffer& vertexBuffer,
                     const VulkanBuffer& indexBuffer,
                     uint32_t indexCount);

    // Resource creation
    bool createVertexBuffer(const void* vertices, VkDeviceSize size,
                           VulkanBuffer& buffer);
    bool createIndexBuffer(const void* indices, VkDeviceSize size,
                          VulkanBuffer& buffer);
    bool createPipeline(const std::string& vertShader,
                       const std::string& fragShader, bool useDescriptors = false);

    // Uniform buffer management
    bool createUniformBuffers();
    bool updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo);
    void bindDescriptorSets();

    // Getters
    VkDevice getDevice() const { return context_.getDevice(); }
    VkPhysicalDevice getPhysicalDevice() const { return context_.getPhysicalDevice(); }
    VmaAllocator getAllocator() const { return context_.getAllocator(); }
    VkCommandPool getCommandPool() const { return commandPool_; }
    VkQueue getGraphicsQueue() const { return context_.getGraphicsQueue(); }
    VkExtent2D getExtent() const { return swapchain_.getExtent(); }
    VkPipelineCache getPipelineCache() const { return pipelineCache_; }
    VkRenderPass getRenderPass() const { return renderPass_; }
    VkImageView getDepthImageView() const { return depthImageView_; }
    uint32_t getCurrentFrameIndex() const { return currentFrame_; }
    uint32_t getFramesInFlight() const { return MAX_FRAMES_IN_FLIGHT; }
    bool isValid() const { return context_.isValid(); }
    bool hasDescriptors() const { return pipeline_.hasDescriptors(); }

private:
    VulkanContext context_;
    VulkanSwapchain swapchain_;
    VulkanPipeline pipeline_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Depth buffer resources
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    // Descriptor sets for uniform buffers
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> descriptorSets_;

    // Uniform buffers (one per frame in flight to avoid sync issues)
    std::array<VulkanBuffer, MAX_FRAMES_IN_FLIGHT> uniformBuffers_;

    // Synchronization objects (pre-allocated, no runtime allocation)
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores_;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedSemaphores_;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences_;

    uint32_t currentFrame_ = 0;

    bool createRenderPass();
    bool createPipelineCache();
    void destroyPipelineCache();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    bool createDescriptorSets();

    // Depth buffer helpers
    VkFormat findDepthFormat();
    bool hasStencilComponent(VkFormat format);
    bool createDepthResources();
    void destroyDepthResources();
};

} // namespace vulkan
} // namespace rendering
} // namespace jupiter
