#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include <array>

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
// VulkanBuffer - Manages Vulkan buffers and memory
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

    bool create(VkDevice device, VkPhysicalDevice physicalDevice,
                VkDeviceSize size, VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties);

    void destroy();

    // Upload data to buffer
    bool upload(const void* data, VkDeviceSize size);

    VkBuffer getBuffer() const { return buffer_; }
    VkDeviceMemory getMemory() const { return memory_; }
    VkDeviceSize getSize() const { return size_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags properties_ = 0;

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
    VulkanPipeline() = default;
    ~VulkanPipeline() { destroy(); }

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    bool create(VkDevice device, VkRenderPass renderPass,
                VkExtent2D extent, const std::string& vertShaderPath,
                const std::string& fragShaderPath);

    void destroy();

    VkPipeline getPipeline() const { return pipeline_; }
    VkPipelineLayout getLayout() const { return layout_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkShaderModule vertShaderModule_ = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule_ = VK_NULL_HANDLE;

    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
    bool loadShaderFile(const std::string& filename, std::vector<uint32_t>& code);
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
    uint32_t getGraphicsFamily() const { return graphicsFamily_; }
    uint32_t getPresentFamily() const { return presentFamily_; }

    bool isValid() const { return device_ != VK_NULL_HANDLE; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    uint32_t graphicsFamily_ = UINT32_MAX;
    uint32_t presentFamily_ = UINT32_MAX;

    bool enableValidation_ = false;

    bool createInstance(const Window& window);
    bool setupDebugMessenger();
    bool createSurface(const Window& window);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
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
                       const std::string& fragShader);

    // Getters
    VkDevice getDevice() const { return context_.getDevice(); }
    VkExtent2D getExtent() const { return swapchain_.getExtent(); }
    bool isValid() const { return context_.isValid(); }

private:
    VulkanContext context_;
    VulkanSwapchain swapchain_;
    VulkanPipeline pipeline_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    // Synchronization objects (pre-allocated, no runtime allocation)
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailableSemaphores_;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> renderFinishedSemaphores_;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlightFences_;

    uint32_t currentFrame_ = 0;

    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
};

} // namespace vulkan
} // namespace rendering
} // namespace jupiter
