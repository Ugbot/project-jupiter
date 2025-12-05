// Define VMA implementation in this translation unit
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
// VMA_DYNAMIC_VULKAN_FUNCTIONS defaults to 1 when STATIC is 0

#include "vulkan_backend.h"
#include "rendering/rendering.h"
#include "rendering/vertex_formats.h"
#include "rendering/descriptor_builder.h"
#include "logging/logging.h"

#include <fstream>
#include <set>
#include <algorithm>
#include <cstring>

namespace jupiter {
namespace rendering {
namespace vulkan {

// ============================================================================
// VulkanBuffer Implementation
// ============================================================================

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
    : allocator_(other.allocator_)
    , device_(other.device_)
    , buffer_(other.buffer_)
    , memory_(other.memory_)
    , allocation_(other.allocation_)
    , size_(other.size_)
    , properties_(other.properties_)
    , usesVMA_(other.usesVMA_)
    , deviceAddress_(other.deviceAddress_) {
    other.allocator_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.allocation_ = VK_NULL_HANDLE;
    other.size_ = 0;
    other.usesVMA_ = false;
    other.deviceAddress_ = 0;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        allocator_ = other.allocator_;
        device_ = other.device_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        allocation_ = other.allocation_;
        size_ = other.size_;
        properties_ = other.properties_;
        usesVMA_ = other.usesVMA_;
        deviceAddress_ = other.deviceAddress_;
        other.allocator_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.size_ = 0;
        other.usesVMA_ = false;
        other.deviceAddress_ = 0;
    }
    return *this;
}

// VMA-based buffer creation (preferred)
bool VulkanBuffer::create(VmaAllocator allocator, VkDeviceSize size,
                          VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                          VmaAllocationCreateFlags flags) {
    LOG_INFO("Vulkan", "Creating VulkanBuffer: allocator=%p, size=%llu, usage=0x%x, memUsage=%d",
             allocator, (unsigned long long)size, usage, memoryUsage);

    if (allocator == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan", "Invalid VMA allocator (VK_NULL_HANDLE)");
        return false;
    }

    if (size == 0) {
        LOG_ERROR("Vulkan", "Invalid buffer size (0)");
        return false;
    }

    allocator_ = allocator;
    size_ = size;
    usesVMA_ = true;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = flags;

    LOG_INFO("Vulkan", "Calling vmaCreateBuffer...");
    VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create buffer with VMA: allocator=%p, size=%llu, usage=0x%x, result=%d",
                  allocator, (unsigned long long)size, usage, result);
        return false;
    }

    LOG_INFO("Vulkan", "vmaCreateBuffer succeeded, buffer=%p", buffer_);

    // Get the memory handle for backward compatibility
    VmaAllocationInfo allocationInfo;
    vmaGetAllocationInfo(allocator, allocation_, &allocationInfo);
    memory_ = allocationInfo.deviceMemory;

    LOG_INFO("Vulkan", "VulkanBuffer created successfully (buffer=%p, memory=%p, size=%llu)",
             buffer_, memory_, (unsigned long long)size);
    return true;
}

// VMA-based buffer creation with device address support (for bindless/raytracing)
bool VulkanBuffer::createWithDeviceAddress(VkDevice device, VmaAllocator allocator,
                                           VkDeviceSize size, VkBufferUsageFlags usage,
                                           VmaMemoryUsage memoryUsage,
                                           VmaAllocationCreateFlags flags) {
    // Ensure shader device address bit is set
    usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    // Create the buffer using VMA
    if (!create(allocator, size, usage, memoryUsage, flags)) {
        return false;
    }

    // Store device handle for address query
    device_ = device;

    // Query the buffer's device address
    VkBufferDeviceAddressInfo addressInfo = {};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer_;
    deviceAddress_ = vkGetBufferDeviceAddress(device, &addressInfo);

    if (deviceAddress_ == 0) {
        LOG_ERROR("Vulkan", "Failed to get buffer device address");
        return false;
    }

    LOG_INFO("Vulkan", "VulkanBuffer created with device address: 0x%llx",
             (unsigned long long)deviceAddress_);
    return true;
}

// Legacy buffer creation (for backward compatibility)
bool VulkanBuffer::create(VkDevice device, VkPhysicalDevice physicalDevice,
                          VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties) {
    device_ = device;
    size_ = size;
    properties_ = properties;
    usesVMA_ = false;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create buffer");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice,
                                               memRequirements.memoryTypeBits,
                                               properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate buffer memory");
        return false;
    }

    vkBindBufferMemory(device, buffer_, memory_, 0);
    return true;
}

void VulkanBuffer::destroy() {
    if (usesVMA_ && allocator_ != VK_NULL_HANDLE) {
        // VMA destruction
        if (buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, buffer_, allocation_);
            buffer_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
            memory_ = VK_NULL_HANDLE;
        }
    } else if (device_ != VK_NULL_HANDLE) {
        // Legacy destruction
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }
}

bool VulkanBuffer::upload(const void* data, VkDeviceSize size) {
    if (size > size_) {
        LOG_ERROR("Vulkan", "Upload size exceeds buffer size");
        return false;
    }

    void* mapped;
    if (usesVMA_) {
        // VMA mapping
        if (vmaMapMemory(allocator_, allocation_, &mapped) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to map buffer memory with VMA");
            return false;
        }
        memcpy(mapped, data, static_cast<size_t>(size));
        vmaUnmapMemory(allocator_, allocation_);
    } else {
        // Legacy mapping
        if (vkMapMemory(device_, memory_, 0, size, 0, &mapped) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to map buffer memory");
            return false;
        }
        memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device_, memory_);
    }

    return true;
}

uint32_t VulkanBuffer::findMemoryType(VkPhysicalDevice physicalDevice,
                                      uint32_t typeFilter,
                                      VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    LOG_ERROR("Vulkan", "Failed to find suitable memory type");
    return 0;
}

// ============================================================================
// VulkanSwapchain Implementation
// ============================================================================

bool VulkanSwapchain::create(VkDevice device, VkPhysicalDevice physicalDevice,
                              VkSurfaceKHR surface, uint32_t width, uint32_t height,
                              VkSwapchainKHR oldSwapchain) {
    device_ = device;

    // Query swapchain support
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    // Choose settings
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    VkExtent2D extent = chooseSwapExtent(capabilities, width, height);

    format_ = surfaceFormat.format;
    extent_ = extent;

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create swapchain");
        return false;
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, images_.data());

    // Create image views
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to create image view");
            return false;
        }
    }

    LOG_INFO("Vulkan", "Created swapchain with %d images (%dx%d)",
             imageCount, extent.width, extent.height);
    return true;
}

void VulkanSwapchain::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        for (auto imageView : imageViews_) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, imageView, nullptr);
            }
        }
        imageViews_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }

        images_.clear();
    }
}

VkSurfaceFormatKHR VulkanSwapchain::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanSwapchain::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::chooseSwapExtent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32_t width, uint32_t height) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {width, height};
    actualExtent.width = std::clamp(actualExtent.width,
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);
    return actualExtent;
}

// ============================================================================
// VulkanPipeline Implementation
// ============================================================================

// New flexible create method
bool VulkanPipeline::create(VkDevice device, VkRenderPass renderPass,
                           VkExtent2D extent, const std::string& vertShaderPath,
                           const std::string& fragShaderPath, const Config& config) {
    device_ = device;
    renderPass_ = renderPass;
    extent_ = extent;
    config_ = config;
    vertShaderPath_ = vertShaderPath;
    fragShaderPath_ = fragShaderPath;

    // Store descriptor set layouts (they're owned externally)
    descriptorSetLayouts_ = config.descriptorSetLayouts;

    // Load shaders
    std::vector<uint32_t> vertCode, fragCode;
    if (!loadShaderFile(vertShaderPath, vertCode)) {
        LOG_ERROR("Vulkan", "Failed to load vertex shader: %s", vertShaderPath.c_str());
        return false;
    }
    if (!loadShaderFile(fragShaderPath, fragCode)) {
        LOG_ERROR("Vulkan", "Failed to load fragment shader: %s", fragShaderPath.c_str());
        return false;
    }

    vertShaderModule_ = createShaderModule(vertCode);
    fragShaderModule_ = createShaderModule(fragCode);

    if (vertShaderModule_ == VK_NULL_HANDLE || fragShaderModule_ == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan", "Failed to create shader modules");
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule_;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule_;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input (flexible)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    if (config.vertexInput != nullptr) {
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexInput->bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = config.vertexInput->bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexInput->attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = config.vertexInput->attributes.data();
    } else {
        // No vertex input
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr;
    }

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = config.blendEnable ? VK_TRUE : VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;  // Standard depth test
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = config.descriptorSetLayouts.empty() ? nullptr : config.descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = config.pushConstants.empty() ? nullptr : config.pushConstants.data();

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &layout_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create pipeline layout");
        return false;
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = layout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &pipeline_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create graphics pipeline");
        return false;
    }

    LOG_INFO("Vulkan", "Created graphics pipeline");
    return true;
}

// Legacy create for backward compatibility
bool VulkanPipeline::create(VkDevice device, VkRenderPass renderPass,
                           VkExtent2D extent, const std::string& vertShaderPath,
                           const std::string& fragShaderPath, bool useDescriptors) {
    // Use 2D vertex format for legacy path
    auto vertexDesc = rendering::Vertex2D::getDescription();

    Config config;
    config.vertexInput = &vertexDesc;

    if (useDescriptors) {
        // Create legacy single UBO descriptor set layout
        rendering::DescriptorSetLayoutBuilder builder(device);
        VkDescriptorSetLayout layout = builder
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
            .build();

        if (layout == VK_NULL_HANDLE) {
            LOG_ERROR("Vulkan", "Failed to create legacy descriptor set layout");
            return false;
        }

        config.descriptorSetLayouts.push_back(layout);
        ownsDescriptorSetLayouts_ = true;  // We created it, we own it
    }

    return create(device, renderPass, extent, vertShaderPath, fragShaderPath, config);
}


void VulkanPipeline::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
        // Only destroy descriptor set layouts if we own them (legacy path)
        if (ownsDescriptorSetLayouts_) {
            for (auto layout : descriptorSetLayouts_) {
                if (layout != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device_, layout, nullptr);
                }
            }
        }
        descriptorSetLayouts_.clear();
        if (vertShaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vertShaderModule_, nullptr);
            vertShaderModule_ = VK_NULL_HANDLE;
        }
        if (fragShaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, fragShaderModule_, nullptr);
            fragShaderModule_ = VK_NULL_HANDLE;
        }
    }
}

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

bool VulkanPipeline::loadShaderFile(const std::string& filename, std::vector<uint32_t>& code) {
    // Try multiple search paths
    const char* searchPaths[] = {
        "",                    // Current directory
        "./shaders/",         // Build directory shaders
        "../shaders/",        // Source directory shaders
        "shaders/",           // Relative shaders
        "./bin/shaders/",     // Binary output shaders
        "../bin/shaders/",    // Parent binary shaders
        "bin/shaders/",       // Relative binary shaders
    };

    for (const char* path : searchPaths) {
        std::string fullPath = std::string(path) + filename;
        std::ifstream file(fullPath, std::ios::ate | std::ios::binary);

        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            code.resize(fileSize / sizeof(uint32_t));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(code.data()), fileSize);
            file.close();
            LOG_INFO("Vulkan", "Loaded shader: %s", fullPath.c_str());
            return true;
        }
    }

    LOG_ERROR("Vulkan", "Failed to find shader file: %s", filename.c_str());
    return false;
}

bool VulkanPipeline::reload(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode) {
    if (device_ == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan", "Cannot reload pipeline - not initialized");
        return false;
    }

    LOG_INFO("Vulkan", "Reloading pipeline shaders...");

    // Wait for device to be idle before recreating pipeline
    vkDeviceWaitIdle(device_);

    // Create new shader modules
    VkShaderModule newVertModule = createShaderModule(vertCode);
    VkShaderModule newFragModule = createShaderModule(fragCode);

    if (newVertModule == VK_NULL_HANDLE || newFragModule == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan", "Failed to create new shader modules for reload");
        if (newVertModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, newVertModule, nullptr);
        }
        if (newFragModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, newFragModule, nullptr);
        }
        return false;
    }

    // Attempt to recreate pipeline with new shaders
    if (!recreatePipeline(newVertModule, newFragModule)) {
        LOG_ERROR("Vulkan", "Failed to recreate pipeline with new shaders");
        vkDestroyShaderModule(device_, newVertModule, nullptr);
        vkDestroyShaderModule(device_, newFragModule, nullptr);
        return false;
    }

    // Success - destroy old shader modules and store new ones
    if (vertShaderModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertShaderModule_, nullptr);
    }
    if (fragShaderModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fragShaderModule_, nullptr);
    }

    vertShaderModule_ = newVertModule;
    fragShaderModule_ = newFragModule;

    LOG_INFO("Vulkan", "Pipeline reloaded successfully");
    return true;
}

bool VulkanPipeline::recreatePipeline(VkShaderModule vertModule, VkShaderModule fragModule) {
    // Destroy old pipeline but keep layout and descriptor set layout
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input (same as original)
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VulkanRenderer::Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2] = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VulkanRenderer::Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VulkanRenderer::Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent_;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Create graphics pipeline with existing layout
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = layout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &pipeline_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to recreate graphics pipeline");
        return false;
    }

    return true;
}

// ============================================================================
// VulkanContext Implementation
// ============================================================================

bool VulkanContext::initialize(const Window& window, bool enableValidation) {
    enableValidation_ = enableValidation;

    if (!createInstance(window)) {
        LOG_ERROR("Vulkan", "Failed to create Vulkan instance");
        return false;
    }

    if (enableValidation_) {
        setupDebugMessenger();
    }

    if (!createSurface(window)) {
        LOG_ERROR("Vulkan", "Failed to create surface");
        return false;
    }

    if (!pickPhysicalDevice()) {
        LOG_ERROR("Vulkan", "Failed to find suitable GPU");
        return false;
    }

    if (!createLogicalDevice()) {
        LOG_ERROR("Vulkan", "Failed to create logical device");
        return false;
    }

    if (!createAllocator()) {
        LOG_ERROR("Vulkan", "Failed to create VMA allocator");
        return false;
    }

    LOG_INFO("Vulkan", "Vulkan context initialized successfully");
    return true;
}

void VulkanContext::destroy() {
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (debugMessenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    LOG_INFO("Vulkan", "Vulkan context destroyed");
}

bool VulkanContext::createInstance(const Window& window) {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Jupiter Engine Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Project Jupiter";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    auto extensions = window.getRequiredExtensions();
    if (enableValidation_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Required for MoltenVK on macOS - add portability extension
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back("VK_KHR_get_physical_device_properties2");

    // Log requested extensions for debugging
    LOG_INFO("Vulkan", "Requesting %zu Vulkan extensions:", extensions.size());
    for (const auto* ext : extensions) {
        LOG_INFO("Vulkan", "  - %s", ext);
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
    if (enableValidation_) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
    }

    // Required for MoltenVK on macOS
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create instance (error code: %d)", result);
        return false;
    }

    LOG_INFO("Vulkan", "Created Vulkan instance");
    return true;
}

bool VulkanContext::setupDebugMessenger() {
    // Debug messenger setup would go here
    // Simplified for now
    return true;
}

bool VulkanContext::createSurface(const Window& window) {
    VkResult result = window.createVulkanSurface(instance_, &surface_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create window surface");
        return false;
    }
    return true;
}

bool VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);

    if (deviceCount == 0) {
        LOG_ERROR("Vulkan", "Failed to find GPUs with Vulkan support");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;

            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            LOG_INFO("Vulkan", "Selected GPU: %s", properties.deviceName);
            return true;
        }
    }

    LOG_ERROR("Vulkan", "Failed to find suitable GPU");
    return false;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) {
    if (!findQueueFamilies(device)) {
        return false;
    }

    // Check for swapchain support
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                        availableExtensions.data());

    bool hasSwapchain = false;
    for (const auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            hasSwapchain = true;
            break;
        }
    }

    return hasSwapchain;
}

bool VulkanContext::findQueueFamilies(VkPhysicalDevice device) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    graphicsFamily_ = UINT32_MAX;
    presentFamily_ = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily_ = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) {
            presentFamily_ = i;
        }

        if (graphicsFamily_ != UINT32_MAX && presentFamily_ != UINT32_MAX) {
            return true;
        }
    }

    return false;
}

bool VulkanContext::createLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {graphicsFamily_, presentFamily_};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Query available extensions
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extensionCount, availableExtensions.data());

    // Build extension list with modern features
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset"  // Required for MoltenVK
    };

    // Check for modern Vulkan extensions (store in member variables for VMA)
    hasDescriptorIndexing_ = false;
    hasBufferDeviceAddress_ = false;
    hasSynchronization2_ = false;

    for (const auto& ext : availableExtensions) {
        if (strcmp(ext.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
            hasDescriptorIndexing_ = true;
            deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
            LOG_INFO("Vulkan", "Enabling VK_EXT_descriptor_indexing for bindless textures");
        }
        if (strcmp(ext.extensionName, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0) {
            hasBufferDeviceAddress_ = true;
            deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            LOG_INFO("Vulkan", "Enabling VK_KHR_buffer_device_address for GPU pointers");
        }
        if (strcmp(ext.extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0) {
            hasSynchronization2_ = true;
            deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            LOG_INFO("Vulkan", "Enabling VK_KHR_synchronization2 for modern barriers");
        }
    }

    // Enable modern features via pNext chain
    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures = {};
    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
    descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;  // For unbounded arrays
    descriptorIndexingFeatures.pNext = nullptr;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    bufferDeviceAddressFeatures.pNext = nullptr;

    VkPhysicalDeviceSynchronization2Features synchronization2Features = {};
    synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    synchronization2Features.synchronization2 = VK_TRUE;
    synchronization2Features.pNext = nullptr;

    // Chain features together
    void* pNext = nullptr;
    if (hasDescriptorIndexing_) {
        descriptorIndexingFeatures.pNext = pNext;
        pNext = &descriptorIndexingFeatures;
    }
    if (hasBufferDeviceAddress_) {
        bufferDeviceAddressFeatures.pNext = pNext;
        pNext = &bufferDeviceAddressFeatures;
    }
    if (hasSynchronization2_) {
        synchronization2Features.pNext = pNext;
        pNext = &synchronization2Features;
    }

    VkPhysicalDeviceFeatures deviceFeatures = {};

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.pNext = pNext;

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create logical device");
        return false;
    }

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);

    LOG_INFO("Vulkan", "Created logical device with modern Vulkan features");
    return true;
}

bool VulkanContext::createAllocator() {
    // VMA is configured for dynamic function loading (VMA_STATIC_VULKAN_FUNCTIONS 0)
    // Provide Vulkan function loaders (VMA will load other functions dynamically)
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    // Don't set vulkanApiVersion - let VMA detect it automatically
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    // Conditionally enable buffer device address if the feature is available
    allocatorInfo.flags = 0;
    if (hasBufferDeviceAddress_) {
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        LOG_INFO("Vulkan", "VMA: Enabling buffer device address support");
    }

    LOG_INFO("Vulkan", "Calling vmaCreateAllocator...");
    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create VMA allocator");
        return false;
    }

    LOG_INFO("Vulkan", "Created VMA allocator");
    return true;
}

// ============================================================================
// VulkanRenderer Implementation
// ============================================================================

bool VulkanRenderer::initialize(const Window& window, bool enableValidation) {
    if (!context_.initialize(window, enableValidation)) {
        return false;
    }

    if (!swapchain_.create(context_.getDevice(), context_.getPhysicalDevice(),
                          context_.getSurface(), window.getWidth(), window.getHeight())) {
        return false;
    }

    // Create depth resources before render pass (render pass needs depth format)
    if (!createDepthResources()) {
        return false;
    }

    if (!createRenderPass()) {
        return false;
    }

    if (!createPipelineCache()) {
        return false;
    }

    if (!createFramebuffers()) {
        return false;
    }

    if (!createCommandPool()) {
        return false;
    }

    if (!createCommandBuffers()) {
        return false;
    }

    if (!createSyncObjects()) {
        return false;
    }

    LOG_INFO("Vulkan", "Vulkan renderer initialized");
    return true;
}

void VulkanRenderer::destroy() {
    if (context_.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.getDevice());

        // Destroy uniform buffers
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            uniformBuffers_[i].destroy();
        }

        // Destroy descriptor pool (also frees descriptor sets)
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context_.getDevice(), descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
        }

        // Destroy sync objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(context_.getDevice(), renderFinishedSemaphores_[i], nullptr);
            }
            if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(context_.getDevice(), imageAvailableSemaphores_[i], nullptr);
            }
            if (inFlightFences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(context_.getDevice(), inFlightFences_[i], nullptr);
            }
        }

        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(context_.getDevice(), commandPool_, nullptr);
        }

        for (auto framebuffer : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(context_.getDevice(), framebuffer, nullptr);
            }
        }

        pipeline_.destroy();

        destroyPipelineCache();

        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(context_.getDevice(), renderPass_, nullptr);
        }

        // Destroy depth resources
        destroyDepthResources();
    }

    swapchain_.destroy();
    context_.destroy();

    LOG_INFO("Vulkan", "Vulkan renderer destroyed");
}

void VulkanRenderer::waitIdle() {
    if (context_.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(context_.getDevice());
    }
}

bool VulkanRenderer::beginFrame(uint32_t& imageIndex) {
    vkWaitForFences(context_.getDevice(), 1, &inFlightFences_[currentFrame_],
                   VK_TRUE, UINT64_MAX);
    vkResetFences(context_.getDevice(), 1, &inFlightFences_[currentFrame_]);

    VkResult result = vkAcquireNextImageKHR(
        context_.getDevice(), swapchain_.getSwapchain(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Vulkan", "Failed to acquire swapchain image");
        return false;
    }

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffers_[currentFrame_], &beginInfo) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to begin command buffer");
        return false;
    }

    return true;
}

void VulkanRenderer::endFrame(uint32_t imageIndex) {
    if (vkEndCommandBuffer(commandBuffers_[currentFrame_]) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to end command buffer");
        return;
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(context_.getGraphicsQueue(), 1, &submitInfo,
                     inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to submit command buffer");
        return;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {swapchain_.getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(context_.getPresentQueue(), &presentInfo);

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::beginRenderPass(uint32_t imageIndex) {
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain_.getExtent();

    VkClearValue clearValues[2] = {};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffers_[currentFrame_], &renderPassInfo,
                        VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRenderer::endRenderPass() {
    vkCmdEndRenderPass(commandBuffers_[currentFrame_]);
}

void VulkanRenderer::bindPipeline() {
    vkCmdBindPipeline(commandBuffers_[currentFrame_],
                     VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.getPipeline());
}

void VulkanRenderer::drawIndexed(const VulkanBuffer& vertexBuffer,
                                 const VulkanBuffer& indexBuffer,
                                 uint32_t indexCount) {
    VkBuffer vertexBuffers[] = {vertexBuffer.getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffers_[currentFrame_], 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffers_[currentFrame_], indexBuffer.getBuffer(),
                        0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffers_[currentFrame_], indexCount, 1, 0, 0, 0);
}

bool VulkanRenderer::createVertexBuffer(const void* vertices, VkDeviceSize size,
                                       VulkanBuffer& buffer) {
    return buffer.create(context_.getDevice(), context_.getPhysicalDevice(), size,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
           && buffer.upload(vertices, size);
}

bool VulkanRenderer::createIndexBuffer(const void* indices, VkDeviceSize size,
                                      VulkanBuffer& buffer) {
    return buffer.create(context_.getDevice(), context_.getPhysicalDevice(), size,
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
           && buffer.upload(indices, size);
}

bool VulkanRenderer::createPipeline(const std::string& vertShader,
                                   const std::string& fragShader, bool useDescriptors) {
    return pipeline_.create(context_.getDevice(), renderPass_,
                           swapchain_.getExtent(), vertShader, fragShader, useDescriptors);
}

bool VulkanRenderer::createRenderPass() {
    // Color attachment
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchain_.getFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Don't need to save depth
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Subpass dependency for depth
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(context_.getDevice(), &renderPassInfo, nullptr,
                          &renderPass_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create render pass");
        return false;
    }

    return true;
}

bool VulkanRenderer::createPipelineCache() {
    // Try to load existing pipeline cache from disk
    std::vector<char> cacheData;
    std::string cacheFilename = "pipeline_cache.bin";
    std::ifstream cacheFile(cacheFilename, std::ios::binary | std::ios::ate);

    if (cacheFile.is_open()) {
        size_t fileSize = static_cast<size_t>(cacheFile.tellg());
        cacheFile.seekg(0);
        cacheData.resize(fileSize);
        cacheFile.read(cacheData.data(), fileSize);
        cacheFile.close();
        LOG_INFO("Vulkan", "Loaded pipeline cache from disk (%zu bytes)", fileSize);
    }

    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.empty() ? nullptr : cacheData.data();

    if (vkCreatePipelineCache(context_.getDevice(), &cacheInfo, nullptr, &pipelineCache_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create pipeline cache");
        return false;
    }

    LOG_INFO("Vulkan", "Created pipeline cache");
    return true;
}

void VulkanRenderer::destroyPipelineCache() {
    if (pipelineCache_ != VK_NULL_HANDLE) {
        // Save pipeline cache to disk before destroying
        size_t cacheSize = 0;
        vkGetPipelineCacheData(context_.getDevice(), pipelineCache_, &cacheSize, nullptr);

        if (cacheSize > 0) {
            std::vector<char> cacheData(cacheSize);
            if (vkGetPipelineCacheData(context_.getDevice(), pipelineCache_, &cacheSize, cacheData.data()) == VK_SUCCESS) {
                std::string cacheFilename = "pipeline_cache.bin";
                std::ofstream cacheFile(cacheFilename, std::ios::binary);
                if (cacheFile.is_open()) {
                    cacheFile.write(cacheData.data(), cacheSize);
                    cacheFile.close();
                    LOG_INFO("Vulkan", "Saved pipeline cache to disk (%zu bytes)", cacheSize);
                }
            }
        }

        vkDestroyPipelineCache(context_.getDevice(), pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
    }
}

bool VulkanRenderer::createFramebuffers() {
    const auto& imageViews = swapchain_.getImageViews();
    framebuffers_.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = {imageViews[i], depthImageView_};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchain_.getExtent().width;
        framebufferInfo.height = swapchain_.getExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(context_.getDevice(), &framebufferInfo, nullptr,
                               &framebuffers_[i]) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to create framebuffer");
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context_.getGraphicsFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(context_.getDevice(), &poolInfo, nullptr,
                           &commandPool_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create command pool");
        return false;
    }

    return true;
}

bool VulkanRenderer::createCommandBuffers() {
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());

    if (vkAllocateCommandBuffers(context_.getDevice(), &allocInfo,
                                commandBuffers_.data()) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate command buffers");
        return false;
    }

    return true;
}

bool VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(context_.getDevice(), &semaphoreInfo, nullptr,
                             &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(context_.getDevice(), &semaphoreInfo, nullptr,
                             &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(context_.getDevice(), &fenceInfo, nullptr,
                         &inFlightFences_[i]) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to create sync objects");
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::createUniformBuffers() {
    if (!pipeline_.hasDescriptors()) {
        return true;  // No uniform buffers needed
    }

    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (!uniformBuffers_[i].create(context_.getDevice(), context_.getPhysicalDevice(),
                                       bufferSize,
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            LOG_ERROR("Vulkan", "Failed to create uniform buffer %zu", i);
            return false;
        }
    }

    LOG_INFO("Vulkan", "Created uniform buffers");
    return true;
}

bool VulkanRenderer::updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo) {
    if (!pipeline_.hasDescriptors()) {
        return true;  // Nothing to update
    }

    if (frameIndex >= MAX_FRAMES_IN_FLIGHT) {
        LOG_ERROR("Vulkan", "Invalid frame index: %u", frameIndex);
        return false;
    }

    return uniformBuffers_[frameIndex].upload(&ubo, sizeof(UniformBufferObject));
}

bool VulkanRenderer::createDescriptorPool() {
    if (!pipeline_.hasDescriptors()) {
        return true;  // No descriptor pool needed
    }

    // Create flexible descriptor pool that can handle UBOs, samplers, and images
    // Size it generously for typical use cases
    rendering::DescriptorPoolBuilder poolBuilder(context_.getDevice());
    poolBuilder.setMaxSets(MAX_FRAMES_IN_FLIGHT * 4)  // Support multiple sets per frame
               .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * 10)
               .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * 20)
               .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * 5);

    descriptorPool_ = poolBuilder.build(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    if (descriptorPool_ == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan", "Failed to create descriptor pool");
        return false;
    }

    LOG_INFO("Vulkan", "Created flexible descriptor pool");
    return true;
}

bool VulkanRenderer::createDescriptorSets() {
    if (!pipeline_.hasDescriptors()) {
        return true;  // No descriptor sets needed
    }

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    const auto& pipelineLayouts = pipeline_.getDescriptorSetLayouts();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = pipelineLayouts.empty() ? VK_NULL_HANDLE : pipelineLayouts[0];
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(context_.getDevice(), &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate descriptor sets");
        return false;
    }

    // Update descriptor sets to point to uniform buffers
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = uniformBuffers_[i].getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(context_.getDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    LOG_INFO("Vulkan", "Created and updated descriptor sets");
    return true;
}

void VulkanRenderer::bindDescriptorSets() {
    if (!pipeline_.hasDescriptors()) {
        return;  // Nothing to bind
    }

    vkCmdBindDescriptorSets(getCurrentCommandBuffer(),
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_.getLayout(),
                            0, 1, &descriptorSets_[currentFrame_],
                            0, nullptr);
}

// ============================================================================
// Depth Buffer Implementation
// ============================================================================

VkFormat VulkanRenderer::findDepthFormat() {
    // Try formats in order of preference
    std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(context_.getPhysicalDevice(), format, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    LOG_ERROR("Vulkan", "Failed to find supported depth format");
    return VK_FORMAT_UNDEFINED;
}

bool VulkanRenderer::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanRenderer::createDepthResources() {
    depthFormat_ = findDepthFormat();
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        return false;
    }

    VkExtent2D extent = swapchain_.getExtent();

    // Create depth image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(context_.getAllocator(), &imageInfo, &allocInfo,
                      &depthImage_, &depthImageAllocation_, nullptr) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create depth image");
        return false;
    }

    // Create depth image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context_.getDevice(), &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create depth image view");
        vmaDestroyImage(context_.getAllocator(), depthImage_, depthImageAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthImageAllocation_ = VK_NULL_HANDLE;
        return false;
    }

    LOG_INFO("Vulkan", "Created depth buffer (%ux%u)", extent.width, extent.height);
    return true;
}

void VulkanRenderer::destroyDepthResources() {
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.getDevice(), depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }

    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(context_.getAllocator(), depthImage_, depthImageAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthImageAllocation_ = VK_NULL_HANDLE;
    }
}

} // namespace vulkan
} // namespace rendering
} // namespace jupiter
