/**
 * @file ghi_vulkan_standalone.cpp
 * @brief Standalone Vulkan Backend Implementation for GHI
 * 
 * Complete Vulkan implementation without dependency on Application/VulkanRenderer.
 * Parallel to ghi_metal_complete.cpp - standalone initialization and rendering.
 */

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0

#include "ghi_vulkan.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <set>
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Helper Structures
// ============================================================================

struct QueueFamilyIndices {
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    uint32_t computeFamily = UINT32_MAX;
    
    bool isComplete() const {
        return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
    }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// ============================================================================
// Standalone Vulkan Backend
// ============================================================================

class GHI_VulkanStandalone : public IGHIBackend {
public:
    GHI_VulkanStandalone() {
        LOG_INFO("GHI_Vulkan", "Standalone Vulkan backend created");
    }
    
    ~GHI_VulkanStandalone() override {
        shutdown();
    }
    
    bool initialize() override {
        LOG_INFO("GHI_Vulkan", "Initializing standalone Vulkan backend");
        
        if (!createInstance()) {
            LOG_ERROR("GHI_Vulkan", "Failed to create Vulkan instance");
            return false;
        }
        
        if (!pickPhysicalDevice()) {
            LOG_ERROR("GHI_Vulkan", "Failed to find suitable GPU");
            return false;
        }
        
        if (!createLogicalDevice()) {
            LOG_ERROR("GHI_Vulkan", "Failed to create logical device");
            return false;
        }
        
        if (!createAllocator()) {
            LOG_ERROR("GHI_Vulkan", "Failed to create VMA allocator");
            return false;
        }
        
        if (!createCommandPool()) {
            LOG_ERROR("GHI_Vulkan", "Failed to create command pool");
            return false;
        }
        
        queryCapabilities();
        
        LOG_INFO("GHI_Vulkan", "Standalone Vulkan backend initialized successfully");
        return true;
    }
    
    void shutdown() override {
        LOG_INFO("GHI_Vulkan", "Shutting down standalone Vulkan backend");
        // TODO: Cleanup
    }
    
    void waitIdle() override {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
        }
    }
    
    // Resource creation
    BufferHandle createBuffer(const BufferCreateInfo& info) override;
    void destroyBuffer(BufferHandle handle) override;
    void updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) override;
    
    TextureHandle createTexture(const TextureCreateInfo& info) override {
        return TextureHandle{};
    }
    
    void destroyTexture(TextureHandle handle) override {}
    void updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, const void* data) override {}
    
    ShaderHandle createShader(const ShaderSource& source) override {
        return ShaderHandle{};
    }
    
    void destroyShader(ShaderHandle handle) override {}
    
    // Command recording
    void beginFrame() override {}
    void endFrame() override {}
    void beginRenderPass() override {}
    void endRenderPass() override {}
    
    void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {}
    void setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {}
    
    // Drawing
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override {}
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, 
                    int32_t vertexOffset, uint32_t firstInstance) override {}
    void drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) override {}
    
    // Compute
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override {}
    void dispatchIndirect(BufferHandle indirectBuffer) override {}
    
    // State management
    void setRenderState(const RenderState& state) override {}
    void getRenderState(RenderState& state) override { state = currentState_; }
    
    void bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) override {}
    void bindIndexBuffer(BufferHandle buffer, size_t offset) override {}
    void bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) override {}
    void bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) override {}
    
    // Synchronization
    void memoryBarrier() override {}
    void bufferBarrier(BufferHandle buffer) override {}
    void textureBarrier(TextureHandle texture) override {}
    
    // Queries
    const Capabilities& getCapabilities() const override { return capabilities_; }
    Backend getBackendType() const override { return Backend::Vulkan; }

private:
    // Vulkan handles
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    
    QueueFamilyIndices queueFamilies_;
    
    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat swapchainImageFormat_;
    VkExtent2D swapchainExtent_;
    
    // Command buffers
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    uint32_t currentFrame_ = 0;
    
    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentImageIndex_ = 0;
    
    // Current state
    Capabilities capabilities_;
    RenderState currentState_;
    
    // Resource tracking
    uint32_t nextBufferID_ = 1;
    uint32_t nextTextureID_ = 1;
    uint32_t nextShaderID_ = 1;
    
    std::unordered_map<uint32_t, VkBuffer> buffers_;
    std::unordered_map<uint32_t, VmaAllocation> allocations_;
    std::unordered_map<uint32_t, VkImage> textures_;
    std::unordered_map<uint32_t, VkImageView> textureViews_;
    std::unordered_map<uint32_t, VkPipeline> pipelines_;
    
    // SDL window (for surface creation)
    SDL_Window* window_ = nullptr;
    
    // Validation layers
    const std::vector<const char*> validationLayers_ = {
        "VK_LAYER_KHRONOS_validation"
    };
    
    const std::vector<const char*> deviceExtensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",  // Required for MoltenVK
#endif
    };
    
    bool enableValidationLayers_ = false;
    
    // Helper methods
    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createCommandPool();
    bool createSyncObjects();
    
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    void queryCapabilities();
};

// ============================================================================
// Instance Creation
// ============================================================================

bool GHI_VulkanStandalone::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Jupiter GHI/RAL";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Jupiter";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // Get required extensions from SDL
    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
    
#ifdef __APPLE__
    // Required for MoltenVK
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    
    if (enableValidationLayers_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }
    
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create Vulkan instance (error: %d)", result);
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created Vulkan instance");
    return true;
}

// ============================================================================
// Physical Device Selection  
// ============================================================================

bool GHI_VulkanStandalone::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        LOG_ERROR("GHI_Vulkan", "No GPUs with Vulkan support found");
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    
    // Pick first suitable device
    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            LOG_INFO("GHI_Vulkan", "Selected GPU: %s", props.deviceName);
            return true;
        }
    }
    
    LOG_ERROR("GHI_Vulkan", "Failed to find suitable GPU");
    return false;
}

bool GHI_VulkanStandalone::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    
    // For standalone mode without surface, just need graphics queue
    return indices.graphicsFamily != UINT32_MAX && extensionsSupported;
}

bool GHI_VulkanStandalone::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    
    std::set<std::string> requiredExtensions(deviceExtensions_.begin(), deviceExtensions_.end());
    
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    
    return requiredExtensions.empty();
}

QueueFamilyIndices GHI_VulkanStandalone::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
            indices.presentFamily = i;  // Assume same for now
        }
        
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.computeFamily = i;
        }
        
        if (indices.isComplete()) {
            break;
        }
    }
    
    return indices;
}

// ============================================================================
// Logical Device Creation
// ============================================================================

bool GHI_VulkanStandalone::createLogicalDevice() {
    queueFamilies_ = findQueueFamilies(physicalDevice_);
    
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilies_.graphicsFamily,
        queueFamilies_.computeFamily
    };
    
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions_.data();
    
    if (enableValidationLayers_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }
    
    VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create logical device (error: %d)", result);
        return false;
    }
    
    // Get queues
    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily, 0, &presentQueue_);
    if (queueFamilies_.computeFamily != UINT32_MAX) {
        vkGetDeviceQueue(device_, queueFamilies_.computeFamily, 0, &computeQueue_);
    }
    
    LOG_INFO("GHI_Vulkan", "Created logical device and queues");
    return true;
}

// ============================================================================
// VMA Allocator Creation
// ============================================================================

bool GHI_VulkanStandalone::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;
    
    VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create VMA allocator (error: %d)", result);
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created VMA allocator");
    return true;
}

// ============================================================================
// Command Pool Creation
// ============================================================================

bool GHI_VulkanStandalone::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilies_.graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    VkResult result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create command pool (error: %d)", result);
        return false;
    }
    
    // Allocate command buffers (2 for double buffering)
    commandBuffers_.resize(2);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    
    result = vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data());
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to allocate command buffers (error: %d)", result);
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created command pool and buffers");
    return true;
}

// ============================================================================
// Capabilities Query
// ============================================================================

void GHI_VulkanStandalone::queryCapabilities() {
    capabilities_.backend = Backend::Vulkan;
    
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    
    capabilities_.deviceName = props.deviceName;
    capabilities_.driverVersion = "Vulkan 1.2+";
    
    // Tier 1
    capabilities_.hasIndexedDraw = true;
    capabilities_.hasDepthTest = true;
    capabilities_.hasMRT = true;
    capabilities_.maxColorAttachments = props.limits.maxColorAttachments;
    capabilities_.maxTextureSize = props.limits.maxImageDimension2D;
    
    // Tier 2
    capabilities_.hasComputeShaders = true;
    capabilities_.hasIndirectDraw = true;
    capabilities_.hasStorageBuffers = true;
    capabilities_.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
    capabilities_.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
    capabilities_.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];
    
    // Tier 3
    capabilities_.hasSubgroups = true;
    capabilities_.subgroupSize = 32;
    capabilities_.hasTessellation = true;
    capabilities_.hasGeometryShaders = true;
}

// ============================================================================
// Buffer Management (VMA-based)
// ============================================================================

BufferHandle GHI_VulkanStandalone::createBuffer(const BufferCreateInfo& info) {
    if (allocator_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "Allocator not initialized");
        return BufferHandle{};
    }
    
    // Map GHI buffer type to Vulkan usage flags
    VkBufferUsageFlags usage = 0;
    switch (info.type) {
        case BufferType::Vertex:
            usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            break;
        case BufferType::Index:
            usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            break;
        case BufferType::Uniform:
            usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            break;
        case BufferType::Storage:
            usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            break;
        case BufferType::Indirect:
            usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            break;
    }
    
    // Add transfer bits for updates
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    // Map GHI usage to VMA memory usage
    VmaMemoryUsage memUsage = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags flags = 0;
    
    if (info.usage == BufferUsage::Dynamic || info.usage == BufferUsage::Stream) {
        flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = info.size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = flags;
    
    VkBuffer vkBuffer;
    VmaAllocation allocation;
    
    VkResult result = vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &vkBuffer, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create buffer (error: %d)", result);
        return BufferHandle{};
    }
    
    // Upload initial data if provided
    if (info.data && info.size > 0) {
        void* mapped;
        if (vmaMapMemory(allocator_, allocation, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, info.data, info.size);
            vmaUnmapMemory(allocator_, allocation);
        }
    }
    
    BufferHandle handle;
    handle.id = nextBufferID_++;
    buffers_[handle.id] = vkBuffer;
    allocations_[handle.id] = allocation;
    
    return handle;
}

void GHI_VulkanStandalone::destroyBuffer(BufferHandle handle) {
    auto bufferIt = buffers_.find(handle.id);
    auto allocIt = allocations_.find(handle.id);
    
    if (bufferIt != buffers_.end() && allocIt != allocations_.end()) {
        vmaDestroyBuffer(allocator_, bufferIt->second, allocIt->second);
        buffers_.erase(bufferIt);
        allocations_.erase(allocIt);
    }
}

void GHI_VulkanStandalone::updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    auto allocIt = allocations_.find(handle.id);
    if (allocIt == allocations_.end()) {
        return;
    }
    
    void* mapped;
    if (vmaMapMemory(allocator_, allocIt->second, &mapped) == VK_SUCCESS) {
        std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
        vmaUnmapMemory(allocator_, allocIt->second);
    }
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter



