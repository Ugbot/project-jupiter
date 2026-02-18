/**
 * @file ghi_vulkan.cpp
 * @brief GHI Vulkan Backend - Standalone Implementation
 * 
 * Complete Vulkan implementation matching Metal backend structure.
 * Uses Jupiter's existing Vulkan utilities + HelloVulkan patterns.
 */

// NOTE: VMA_IMPLEMENTATION is already defined in vulkan_backend.cpp
// Do NOT define it again here to avoid duplicate symbols
#define VMA_STATIC_VULKAN_FUNCTIONS 0

#include "ghi_vulkan.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <array>
#include <set>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cmath>

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Constructor / Destructor
// ============================================================================

// ============================================================================
// Helpers
// ============================================================================

static VkBlendFactor convertBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp convertBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return VK_BLEND_OP_ADD;
        case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min: return VK_BLEND_OP_MIN;
        case BlendOp::Max: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp convertCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case CompareOp::Less: return VK_COMPARE_OP_LESS;
        case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS;
    }
}

static VkCullModeFlags convertCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None: return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_BACK_BIT;
    }
}

static VkFrontFace convertFrontFace(FrontFace face) {
    switch (face) {
        case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
        case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

GHI_VulkanBackend::GHI_VulkanBackend() {
    LOG_INFO("GHI_Vulkan", "Standalone Vulkan backend created");
}

GHI_VulkanBackend::~GHI_VulkanBackend() {
    shutdown();
}

// ============================================================================
// Initialization (Standalone - like Metal)
// ============================================================================

bool GHI_VulkanBackend::initialize() {
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
    
    if (!createSyncObjects()) {
        LOG_ERROR("GHI_Vulkan", "Failed to create sync objects");
        return false;
    }
    
    if (!createDescriptorSetLayouts()) {
        LOG_ERROR("GHI_Vulkan", "Failed to create descriptor set layouts");
        return false;
    }
    
    if (!createDescriptorPool()) {
        LOG_ERROR("GHI_Vulkan", "Failed to create descriptor pool");
        return false;
    }
    
    if (!allocateDescriptorSets()) {
        LOG_ERROR("GHI_Vulkan", "Failed to allocate descriptor sets");
        return false;
    }
    
    queryCapabilities();
    
    LOG_INFO("GHI_Vulkan", "Standalone Vulkan backend initialized successfully");
    LOG_INFO("GHI_Vulkan", "  Device: %s", capabilities_.deviceName.c_str());
    
    return true;
}

void GHI_VulkanBackend::shutdown() {
    if (device_ == VK_NULL_HANDLE) return;
    
    LOG_INFO("GHI_Vulkan", "Shutting down Vulkan backend");
    
    vkDeviceWaitIdle(device_);
    
    // Cleanup buffer resources
    for (auto& [id, buffer] : buffers_) {
        auto allocIt = bufferAllocations_.find(id);
        if (allocIt != bufferAllocations_.end()) {
            vmaDestroyBuffer(allocator_, buffer, allocIt->second);
        }
    }
    buffers_.clear();
    bufferAllocations_.clear();
    
    // Cleanup texture resources (samplers, image views, images)
    for (auto& [id, sampler] : samplers_) {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler, nullptr);
        }
    }
    samplers_.clear();
    
    // Cleanup standalone samplers
    for (auto& [id, sampler] : standaloneSamplers_) {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler, nullptr);
        }
    }
    standaloneSamplers_.clear();
    
    for (auto& [id, imageView] : imageViews_) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, imageView, nullptr);
        }
    }
    imageViews_.clear();
    
    for (auto& [id, image] : images_) {
        auto allocIt = imageAllocations_.find(id);
        if (allocIt != imageAllocations_.end()) {
            vmaDestroyImage(allocator_, image, allocIt->second);
        }
    }
    images_.clear();
    imageAllocations_.clear();
    
    // Cleanup depth resources
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE && depthImageAllocation_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, depthImage_, depthImageAllocation_);
        depthImage_ = VK_NULL_HANDLE;
        depthImageAllocation_ = VK_NULL_HANDLE;
    }
    
    // Cleanup framebuffers
    for (auto& fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
    }
    framebuffers_.clear();
    
    // Cleanup render pass
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    
    // Cleanup swapchain image views
    for (auto& view : swapchainImageViews_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    swapchainImageViews_.clear();
    
    // Cleanup swapchain
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    // Release shader modules
    for (auto& [id, data] : shaderModules_) {
        if (data.vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, data.vertModule, nullptr);
        if (data.fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, data.fragModule, nullptr);
    }
    shaderModules_.clear();

    // Release pipeline cache
    for (auto& [key, pipeline] : pipelineCache_) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    }
    pipelineCache_.clear();

    for (auto& [id, layout] : pipelineLayouts_) {
        if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, layout, nullptr);
    }
    pipelineLayouts_.clear();
    
    // Cleanup sync objects
    for (auto& fence : inFlightFences_) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence, nullptr);
        }
    }
    for (auto& sem : imageAvailableSemaphores_) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, sem, nullptr);
        }
    }
    for (auto& sem : renderFinishedSemaphores_) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, sem, nullptr);
        }
    }
    
    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }
    
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void GHI_VulkanBackend::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

void GHI_VulkanBackend::queryCapabilities() {
    capabilities_.backend = Backend::Vulkan;
    
    // Query device properties
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    
    capabilities_.deviceName = props.deviceName;
    capabilities_.driverVersion = "Vulkan 1.2+";
    
    // Tier 1 - Always available in Vulkan 1.2
    capabilities_.hasIndexedDraw = true;
    capabilities_.hasDepthTest = true;
    capabilities_.hasMRT = true;
    capabilities_.maxColorAttachments = props.limits.maxColorAttachments;
    capabilities_.maxTextureSize = props.limits.maxImageDimension2D;
    
    // Tier 2 - Vulkan 1.2 features (near-common denominator)
    capabilities_.hasComputeShaders = true;
    capabilities_.hasIndirectDraw = true;
    capabilities_.hasStorageBuffers = true;
    capabilities_.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
    capabilities_.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
    capabilities_.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];
    
    // Tier 3 - Optimization features
    capabilities_.hasSubgroups = true;  // Vulkan 1.1+
    capabilities_.subgroupSize = 32;    // Typical, TODO: query VkPhysicalDeviceSubgroupProperties
    capabilities_.hasTessellation = true;
    capabilities_.hasGeometryShaders = true;
    
    // Tier 4 - Optional features
    // Ray tracing requires extension check
    capabilities_.hasRayTracing = false;  // TODO: Check for VK_KHR_ray_tracing
    capabilities_.hasMeshShaders = false; // TODO: Check for VK_EXT_mesh_shader
}

BufferHandle GHI_VulkanBackend::createBuffer(const BufferCreateInfo& info) {
    if (!allocator_) {
        LOG_ERROR("GHI_Vulkan", "Cannot create buffer: allocator not initialized");
        return BufferHandle{};
    }
    
    // Convert GHI buffer type to Vulkan usage flags
    VkBufferUsageFlags usage = 0;
    switch (info.type) {
        case BufferType::Vertex:
            usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case BufferType::Index:
            usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            break;
        case BufferType::Uniform:
            usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            break;
        case BufferType::Storage:
            usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            break;
        case BufferType::Indirect:
            usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            break;
    }
    
    // Create VkBuffer via VMA
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = info.size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    
    // For buffers with initial data, or dynamic buffers, we need host access
    if (info.data != nullptr || info.usage == BufferUsage::Dynamic) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    
    VkBuffer buffer;
    VmaAllocation allocation;
    
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create buffer (size=%zu)", info.size);
        return BufferHandle{};
    }
    
    // Upload initial data if provided
    if (info.data && info.size > 0) {
        void* mapped;
        VkResult mapResult = vmaMapMemory(allocator_, allocation, &mapped);
        if (mapResult == VK_SUCCESS) {
            memcpy(mapped, info.data, info.size);
            vmaUnmapMemory(allocator_, allocation);
            LOG_INFO("GHI_Vulkan", "Uploaded %zu bytes to buffer", info.size);
        } else {
            LOG_ERROR("GHI_Vulkan", "Failed to map buffer for upload (error=%d, size=%zu)", mapResult, info.size);
        }
    }
    
    BufferHandle handle;
    handle.id = nextBufferID_++;
    buffers_[handle.id] = buffer;
    bufferAllocations_[handle.id] = allocation;
    
    LOG_INFO("GHI_Vulkan", "Created buffer: id=%u, size=%zu, type=%d", handle.id, info.size, (int)info.type);
    
    return handle;
}

void GHI_VulkanBackend::destroyBuffer(BufferHandle handle) {
    auto bufIt = buffers_.find(handle.id);
    auto allocIt = bufferAllocations_.find(handle.id);
    
    if (bufIt != buffers_.end() && allocIt != bufferAllocations_.end()) {
        vmaDestroyBuffer(allocator_, bufIt->second, allocIt->second);
        buffers_.erase(bufIt);
        bufferAllocations_.erase(allocIt);
        LOG_INFO("GHI_Vulkan", "Destroyed buffer: id=%u", handle.id);
    }
}

void GHI_VulkanBackend::updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    auto allocIt = bufferAllocations_.find(handle.id);
    if (allocIt == bufferAllocations_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid buffer handle: %u", handle.id);
        return;
    }
    
    void* mapped;
    if (vmaMapMemory(allocator_, allocIt->second, &mapped) == VK_SUCCESS) {
        memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
        vmaUnmapMemory(allocator_, allocIt->second);
    } else {
        LOG_ERROR("GHI_Vulkan", "Failed to map buffer memory");
    }
}

TextureHandle GHI_VulkanBackend::createTexture(const TextureCreateInfo& info) {
    if (!allocator_ || !device_) {
        LOG_ERROR("GHI_Vulkan", "Cannot create texture: allocator or device not initialized");
        return TextureHandle{};
    }
    
    // Convert GHI format to Vulkan format
    VkFormat vkFormat = convertFormat(info.format);
    
    // Determine image type
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    uint32_t arrayLayers = 1;
    
    switch (info.type) {
        case TextureType::Texture2D:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_2D;
            break;
        case TextureType::TextureCube:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            arrayLayers = 6;
            break;
        case TextureType::Texture3D:
            imageType = VK_IMAGE_TYPE_3D;
            viewType = VK_IMAGE_VIEW_TYPE_3D;
            break;
        case TextureType::TextureArray:
            imageType = VK_IMAGE_TYPE_2D;
            viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            arrayLayers = info.depth;
            break;
    }
    
    // Determine usage flags
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasUsage(info.usage, TextureUsage::Sampled)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (hasUsage(info.usage, TextureUsage::Storage)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (hasUsage(info.usage, TextureUsage::RenderTarget)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (hasUsage(info.usage, TextureUsage::DepthStencil)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (hasUsage(info.usage, TextureUsage::TransferSrc)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    
    // Calculate mip levels
    uint32_t mipLevels = info.mipLevels;
    if (mipLevels == 0) {
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(info.width, info.height)))) + 1;
    }
    
    // Create VkImage via VMA
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = imageType;
    imageInfo.format = vkFormat;
    imageInfo.extent.width = info.width;
    imageInfo.extent.height = info.height;
    imageInfo.extent.depth = (info.type == TextureType::Texture3D) ? info.depth : 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    if (info.type == TextureType::TextureCube) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    VkImage image;
    VmaAllocation allocation;
    
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create texture image: %ux%u", info.width, info.height);
        return TextureHandle{};
    }
    
    // Determine aspect mask
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (info.format == Format::Depth16 || info.format == Format::Depth24 || 
        info.format == Format::Depth32F) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else if (info.format == Format::Depth24_Stencil8 || info.format == Format::Depth32F_Stencil8) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    
    // Create VkImageView
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = vkFormat;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = arrayLayers;
    
    VkImageView imageView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create texture image view");
        vmaDestroyImage(allocator_, image, allocation);
        return TextureHandle{};
    }
    
    // Create sampler for this texture
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    
    // Convert filter modes
    switch (info.magFilter) {
        case Filter::Nearest:
        case Filter::Nearest_Mipmap_Nearest:
        case Filter::Nearest_Mipmap_Linear:
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            break;
        default:
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            break;
    }
    
    switch (info.minFilter) {
        case Filter::Nearest:
        case Filter::Nearest_Mipmap_Nearest:
        case Filter::Nearest_Mipmap_Linear:
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            break;
        default:
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            break;
    }
    
    // Mipmap mode
    switch (info.minFilter) {
        case Filter::Nearest_Mipmap_Nearest:
        case Filter::Linear_Mipmap_Nearest:
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        default:
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
    }
    
    // Address modes
    auto convertWrapMode = [](WrapMode mode) -> VkSamplerAddressMode {
        switch (mode) {
            case WrapMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case WrapMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case WrapMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case WrapMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };
    
    samplerInfo.addressModeU = convertWrapMode(info.wrapS);
    samplerInfo.addressModeV = convertWrapMode(info.wrapT);
    samplerInfo.addressModeW = convertWrapMode(info.wrapR);
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    samplerInfo.mipLodBias = 0.0f;
    
    VkSampler sampler;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create texture sampler");
        vkDestroyImageView(device_, imageView, nullptr);
        vmaDestroyImage(allocator_, image, allocation);
        return TextureHandle{};
    }
    
    // Upload initial data if provided
    if (info.data && info.width > 0 && info.height > 0) {
        // Calculate data size (assume RGBA8 for now, TODO: handle all formats)
        size_t pixelSize = 4;  // RGBA8
        if (info.format == Format::R8_UNORM) pixelSize = 1;
        else if (info.format == Format::RG8_UNORM) pixelSize = 2;
        else if (info.format == Format::RGB8_UNORM || info.format == Format::RGB8_SRGB) pixelSize = 3;
        else if (info.format == Format::R16_FLOAT) pixelSize = 2;
        else if (info.format == Format::RG16_FLOAT) pixelSize = 4;
        else if (info.format == Format::RGBA16_FLOAT) pixelSize = 8;
        else if (info.format == Format::R32_FLOAT) pixelSize = 4;
        else if (info.format == Format::RG32_FLOAT) pixelSize = 8;
        else if (info.format == Format::RGB32_FLOAT) pixelSize = 12;
        else if (info.format == Format::RGBA32_FLOAT) pixelSize = 16;
        
        size_t dataSize = info.width * info.height * pixelSize;
        
        // Create staging buffer
        VkBufferCreateInfo stagingBufferInfo = {};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = dataSize;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocationCreateInfo stagingAllocInfo = {};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
                                  VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        VmaAllocationInfo stagingAllocInfoResult;
        
        if (vmaCreateBuffer(allocator_, &stagingBufferInfo, &stagingAllocInfo, 
                           &stagingBuffer, &stagingAllocation, &stagingAllocInfoResult) == VK_SUCCESS) {
            
            // Copy data to staging buffer
            memcpy(stagingAllocInfoResult.pMappedData, info.data, dataSize);
            
            // Transition image layout and copy
            VkCommandBuffer cmdBuffer = commandBuffers_[currentFrame_];
            
            // Begin one-time command buffer
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            
            vkResetCommandBuffer(cmdBuffer, 0);
            vkBeginCommandBuffer(cmdBuffer, &beginInfo);
            
            // Transition to TRANSFER_DST_OPTIMAL
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = aspectMask;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = arrayLayers;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            
            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            
            // Copy buffer to image
            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = aspectMask;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {info.width, info.height, 1};
            
            vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            
            // Transition to SHADER_READ_ONLY_OPTIMAL
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            
            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            
            vkEndCommandBuffer(cmdBuffer);
            
            // Submit and wait
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmdBuffer;
            
            vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(graphicsQueue_);
            
            // Cleanup staging buffer
            vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
            
            LOG_INFO("GHI_Vulkan", "Uploaded texture data: %ux%u, %zu bytes", 
                     info.width, info.height, dataSize);
        }
    }
    
    // Store handles
    TextureHandle handle;
    handle.id = nextTextureID_++;
    images_[handle.id] = image;
    imageAllocations_[handle.id] = allocation;
    imageViews_[handle.id] = imageView;
    samplers_[handle.id] = sampler;
    
    LOG_INFO("GHI_Vulkan", "Created texture: id=%u, size=%ux%u, format=%d, mips=%u", 
             handle.id, info.width, info.height, (int)info.format, mipLevels);
    
    return handle;
}

void GHI_VulkanBackend::destroyTexture(TextureHandle handle) {
    if (!handle.isValid()) return;
    
    // Wait for GPU to finish using the texture
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
    
    // Destroy sampler
    auto samplerIt = samplers_.find(handle.id);
    if (samplerIt != samplers_.end() && samplerIt->second != VK_NULL_HANDLE) {
        vkDestroySampler(device_, samplerIt->second, nullptr);
        samplers_.erase(samplerIt);
    }
    
    // Destroy image view
    auto viewIt = imageViews_.find(handle.id);
    if (viewIt != imageViews_.end() && viewIt->second != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, viewIt->second, nullptr);
        imageViews_.erase(viewIt);
    }
    
    // Destroy image and allocation
    auto imageIt = images_.find(handle.id);
    auto allocIt = imageAllocations_.find(handle.id);
    if (imageIt != images_.end() && allocIt != imageAllocations_.end()) {
        vmaDestroyImage(allocator_, imageIt->second, allocIt->second);
        images_.erase(imageIt);
        imageAllocations_.erase(allocIt);
    }
    
    LOG_INFO("GHI_Vulkan", "Destroyed texture: id=%u", handle.id);
}

void GHI_VulkanBackend::updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y,
                                     uint32_t width, uint32_t height, const void* data) {
    if (!handle.isValid() || !data || width == 0 || height == 0) {
        return;
    }
    
    auto imageIt = images_.find(handle.id);
    if (imageIt == images_.end()) {
        LOG_ERROR("GHI_Vulkan", "updateTexture: Invalid texture handle %u", handle.id);
        return;
    }
    
    VkImage image = imageIt->second;
    
    // Assume RGBA8 for size calculation (TODO: track format per texture)
    size_t pixelSize = 4;
    size_t dataSize = width * height * pixelSize;
    
    // Create staging buffer
    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = dataSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingAllocInfoResult;
    
    if (vmaCreateBuffer(allocator_, &stagingBufferInfo, &stagingAllocInfo,
                       &stagingBuffer, &stagingAllocation, &stagingAllocInfoResult) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create staging buffer for texture update");
        return;
    }
    
    // Copy data to staging buffer
    memcpy(stagingAllocInfoResult.pMappedData, data, dataSize);
    
    // Record copy commands
    VkCommandBuffer cmdBuffer = commandBuffers_[currentFrame_];
    
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkResetCommandBuffer(cmdBuffer, 0);
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    
    // Transition to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = level;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image region
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition back to SHADER_READ_ONLY_OPTIMAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(cmdBuffer);
    
    // Submit and wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    
    // Cleanup staging buffer
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
    
    LOG_INFO("GHI_Vulkan", "Updated texture: id=%u, region=(%u,%u %ux%u), level=%u",
             handle.id, x, y, width, height, level);
}

ShaderHandle GHI_VulkanBackend::createShader(const ShaderSource& source) {
    // Load SPIR-V shaders
    std::vector<uint32_t> vertCode, fragCode;
    
    if (source.vertexPath) {
        if (!loadSPIRV(source.vertexPath, vertCode)) {
            LOG_ERROR("GHI_Vulkan", "Failed to load vertex shader: %s", source.vertexPath);
            return ShaderHandle{};
        }
    }
    
    if (source.fragmentPath) {
        if (!loadSPIRV(source.fragmentPath, fragCode)) {
            LOG_ERROR("GHI_Vulkan", "Failed to load fragment shader: %s", source.fragmentPath);
            return ShaderHandle{};
        }
    }
    
    // Create shader modules
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);
    
    if (!vertModule || !fragModule) {
        LOG_ERROR("GHI_Vulkan", "Failed to create shader modules");
        if (vertModule) vkDestroyShaderModule(device_, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device_, fragModule, nullptr);
        return ShaderHandle{};
    }
    
    // Store modules for later pipeline creation
    VulkanShaderData data;
    data.vertModule = vertModule;
    data.fragModule = fragModule;

    ShaderHandle handle;
    handle.id = nextShaderID_++;
    shaderModules_[handle.id] = data;

    LOG_INFO("GHI_Vulkan", "Created shader modules (deferred pipeline creation): id=%u", handle.id);
    return handle;
}

void GHI_VulkanBackend::destroyShader(ShaderHandle handle) {
    if (!handle.isValid()) return;

    // Destroy cached pipelines for this shader
    for (auto it = pipelineCache_.begin(); it != pipelineCache_.end(); ) {
        if (it->first.shaderId == handle.id) {
            if (it->second != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, it->second, nullptr);
            }
            it = pipelineCache_.erase(it);
        } else {
            ++it;
        }
    }

    // Destroy layout
    auto layoutIt = pipelineLayouts_.find(handle.id);
    if (layoutIt != pipelineLayouts_.end()) {
        vkDestroyPipelineLayout(device_, layoutIt->second, nullptr);
        pipelineLayouts_.erase(layoutIt);
    }

    // Destroy modules
    auto moduleIt = shaderModules_.find(handle.id);
    if (moduleIt != shaderModules_.end()) {
        if (moduleIt->second.vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, moduleIt->second.vertModule, nullptr);
        if (moduleIt->second.fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device_, moduleIt->second.fragModule, nullptr);
        shaderModules_.erase(moduleIt);
    }
}

// ============================================================================
// Sampler Management
// ============================================================================

SamplerHandle GHI_VulkanBackend::createSampler(const SamplerCreateInfo& info) {
    if (!device_) {
        LOG_ERROR("GHI_Vulkan", "Cannot create sampler: device not initialized");
        return SamplerHandle{};
    }
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    
    // Convert filter modes
    auto toVkFilter = [](Filter filter) -> VkFilter {
        switch (filter) {
            case Filter::Nearest:
            case Filter::Nearest_Mipmap_Nearest:
            case Filter::Nearest_Mipmap_Linear:
                return VK_FILTER_NEAREST;
            default:
                return VK_FILTER_LINEAR;
        }
    };
    
    samplerInfo.magFilter = toVkFilter(info.magFilter);
    samplerInfo.minFilter = toVkFilter(info.minFilter);
    
    // Mipmap mode
    switch (info.mipFilter) {
        case Filter::Nearest_Mipmap_Nearest:
        case Filter::Linear_Mipmap_Nearest:
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        default:
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
    }
    
    // Address modes
    auto toVkAddressMode = [](WrapMode mode) -> VkSamplerAddressMode {
        switch (mode) {
            case WrapMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case WrapMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case WrapMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case WrapMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };
    
    samplerInfo.addressModeU = toVkAddressMode(info.wrapS);
    samplerInfo.addressModeV = toVkAddressMode(info.wrapT);
    samplerInfo.addressModeW = toVkAddressMode(info.wrapR);
    
    // Anisotropy
    samplerInfo.anisotropyEnable = info.anisotropyEnabled ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = info.maxAnisotropy;
    
    // LOD settings
    samplerInfo.minLod = info.minLod;
    samplerInfo.maxLod = info.maxLod;
    samplerInfo.mipLodBias = info.mipLodBias;
    
    // Compare (for shadow maps)
    samplerInfo.compareEnable = info.compareEnabled ? VK_TRUE : VK_FALSE;
    
    auto toVkCompareOp = [](CompareOp op) -> VkCompareOp {
        switch (op) {
            case CompareOp::Never: return VK_COMPARE_OP_NEVER;
            case CompareOp::Less: return VK_COMPARE_OP_LESS;
            case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
            default: return VK_COMPARE_OP_LESS;
        }
    };
    samplerInfo.compareOp = toVkCompareOp(info.compareOp);
    
    // Border color
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    
    VkSampler sampler;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create sampler");
        return SamplerHandle{};
    }
    
    SamplerHandle handle;
    handle.id = nextSamplerID_++;
    standaloneSamplers_[handle.id] = sampler;
    
    LOG_INFO("GHI_Vulkan", "Created sampler: id=%u, aniso=%s", 
             handle.id, info.anisotropyEnabled ? "yes" : "no");
    
    return handle;
}

void GHI_VulkanBackend::destroySampler(SamplerHandle handle) {
    if (!handle.isValid()) return;
    
    auto it = standaloneSamplers_.find(handle.id);
    if (it != standaloneSamplers_.end()) {
        if (device_) {
            vkDestroySampler(device_, it->second, nullptr);
        }
        standaloneSamplers_.erase(it);
        LOG_INFO("GHI_Vulkan", "Destroyed sampler: id=%u", handle.id);
    }
}

void GHI_VulkanBackend::bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    if (!sampler.isValid()) {
        LOG_WARN("GHI_Vulkan", "bindSampler: Invalid sampler handle");
        return;
    }
    
    auto it = standaloneSamplers_.find(sampler.id);
    if (it == standaloneSamplers_.end()) {
        LOG_ERROR("GHI_Vulkan", "bindSampler: Sampler not found: id=%u", sampler.id);
        return;
    }
    
    // Note: In modern Vulkan, samplers are typically bound via descriptor sets
    // alongside textures. This implementation updates the descriptor set.
    uint32_t descriptorIndex = currentFrame_ * 2 + set;
    if (descriptorIndex >= descriptorSets_.size()) {
        LOG_ERROR("GHI_Vulkan", "Invalid descriptor set index for sampler");
        return;
    }
    
    // For standalone sampler binding, we need a separate sampler descriptor type
    // For now, log the binding - full implementation needs VK_DESCRIPTOR_TYPE_SAMPLER
    LOG_INFO("GHI_Vulkan", "Bound sampler: id=%u to set=%u binding=%u", 
             sampler.id, set, binding);
}

void GHI_VulkanBackend::beginFrame() {
    currentCommandBuffer_ = commandBuffers_[currentFrame_];
    
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
    
    // Acquire swapchain image if swapchain exists
    if (swapchain_ != VK_NULL_HANDLE) {
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                imageAvailableSemaphores_[currentFrame_],
                                                VK_NULL_HANDLE, &currentImageIndex_);
        if (result != VK_SUCCESS) {
            LOG_WARN("GHI_Vulkan", "Failed to acquire swapchain image (error: %d)", result);
            return;
        }
    }
    
    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
    vkResetCommandBuffer(currentCommandBuffer_, 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(currentCommandBuffer_, &beginInfo);
}

void GHI_VulkanBackend::endFrame() {
    vkEndCommandBuffer(currentCommandBuffer_);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = swapchain_ != VK_NULL_HANDLE ? 1 : 0;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &currentCommandBuffer_;
    
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
    submitInfo.signalSemaphoreCount = swapchain_ != VK_NULL_HANDLE ? 1 : 0;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    VkResult submitResult = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]);
    if (submitResult != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to submit command buffer (error: %d)", submitResult);
    }
    
    // Present if we have swapchain
    if (swapchain_ != VK_NULL_HANDLE) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        
        VkSwapchainKHR swapchains[] = {swapchain_};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &currentImageIndex_;
        
        VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
            LOG_ERROR("GHI_Vulkan", "Failed to present (error: %d)", presentResult);
        }
    }
    
    vkQueueWaitIdle(graphicsQueue_);
    
    currentCommandBuffer_ = VK_NULL_HANDLE;
    currentFrame_ = (currentFrame_ + 1) % 2;
}

void GHI_VulkanBackend::beginRenderPass() {
    if (renderPass_ == VK_NULL_HANDLE) {
        LOG_WARN("GHI_Vulkan", "Cannot begin render pass: no render pass created");
        return;
    }
    
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_WARN("GHI_Vulkan", "Cannot begin render pass: no command buffer");
        return;
    }
    
    if (currentImageIndex_ >= framebuffers_.size()) {
        LOG_ERROR("GHI_Vulkan", "Invalid image index: %u (max: %zu)", currentImageIndex_, framebuffers_.size());
        return;
    }
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[currentImageIndex_];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent_;
    
    // Clear values for color and depth attachments
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{
        currentState_.clearColor.r,
        currentState_.clearColor.g,
        currentState_.clearColor.b,
        currentState_.clearColor.a
    }};
    clearValues[1].depthStencil = {1.0f, 0};  // Clear depth to 1.0 (far plane)
    
    // Debug: Log clear color (first frame only)
    static bool logged = false;
    if (!logged) {
        LOG_INFO("GHI_Vulkan", "beginRenderPass: clear=(%0.2f,%0.2f,%0.2f), extent=%ux%u, fb=%zu",
                 currentState_.clearColor.r, currentState_.clearColor.g, currentState_.clearColor.b,
                 swapchainExtent_.width, swapchainExtent_.height, framebuffers_.size());
        logged = true;
    }
    
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(currentCommandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void GHI_VulkanBackend::beginRenderPass(RenderTargetHandle target) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_WARN("GHI_Vulkan", "Cannot begin render pass: no command buffer");
        return;
    }
    
    if (!target.isValid()) {
        // Fall back to swapchain render pass
        beginRenderPass();
        return;
    }
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid render target handle: %u", target.id);
        return;
    }
    
    RenderTargetData& rt = it->second;
    currentRenderTarget_ = target;
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = rt.renderPass;
    renderPassInfo.framebuffer = rt.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {rt.width, rt.height};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(rt.clearValues.size());
    renderPassInfo.pClearValues = rt.clearValues.data();
    
    vkCmdBeginRenderPass(currentCommandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    LOG_INFO("GHI_Vulkan", "Begin render pass to target: id=%u, size=%ux%u", 
             target.id, rt.width, rt.height);
}

void GHI_VulkanBackend::endRenderPass() {
    if (currentCommandBuffer_ != VK_NULL_HANDLE) {
        vkCmdEndRenderPass(currentCommandBuffer_);
    }
    currentRenderTarget_ = RenderTargetHandle{};  // Clear current target
}

// ============================================================================
// Render Target Management
// ============================================================================

RenderTargetHandle GHI_VulkanBackend::createRenderTarget(const RenderTargetCreateInfo& info) {
    if (!device_ || !allocator_) {
        LOG_ERROR("GHI_Vulkan", "Cannot create render target: device not initialized");
        return RenderTargetHandle{};
    }
    
    if (info.width == 0 || info.height == 0) {
        LOG_ERROR("GHI_Vulkan", "Cannot create render target: invalid dimensions");
        return RenderTargetHandle{};
    }
    
    RenderTargetData rt;
    rt.width = info.width;
    rt.height = info.height;
    
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorRefs;
    std::vector<VkImageView> framebufferViews;
    
    // Create color attachments
    for (size_t i = 0; i < info.colorAttachments.size(); i++) {
        const auto& colorAttach = info.colorAttachments[i];
        
        // Create texture for this attachment
        TextureCreateInfo texInfo;
        texInfo.type = TextureType::Texture2D;
        texInfo.format = colorAttach.format;
        texInfo.width = info.width;
        texInfo.height = info.height;
        texInfo.mipLevels = 1;
        texInfo.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        
        TextureHandle colorTex = createTexture(texInfo);
        if (!colorTex.isValid()) {
            LOG_ERROR("GHI_Vulkan", "Failed to create color attachment %zu", i);
            // Cleanup already created textures
            for (auto& tex : rt.colorTextures) {
                destroyTexture(tex);
            }
            return RenderTargetHandle{};
        }
        rt.colorTextures.push_back(colorTex);
        
        // Get image view for framebuffer
        auto viewIt = imageViews_.find(colorTex.id);
        if (viewIt != imageViews_.end()) {
            framebufferViews.push_back(viewIt->second);
        }
        
        // Attachment description
        VkAttachmentDescription attachDesc{};
        attachDesc.format = convertFormat(colorAttach.format);
        attachDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attachDesc.loadOp = colorAttach.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments.push_back(attachDesc);
        
        // Color reference
        VkAttachmentReference colorRef{};
        colorRef.attachment = static_cast<uint32_t>(i);
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(colorRef);
        
        // Clear value
        VkClearValue clearValue{};
        clearValue.color = {{colorAttach.clearColor.r, colorAttach.clearColor.g, 
                            colorAttach.clearColor.b, colorAttach.clearColor.a}};
        rt.clearValues.push_back(clearValue);
    }
    
    // Create depth attachment if requested
    VkAttachmentReference depthRef{};
    if (info.hasDepth) {
        TextureCreateInfo depthInfo;
        depthInfo.type = TextureType::Texture2D;
        depthInfo.format = info.depthFormat;
        depthInfo.width = info.width;
        depthInfo.height = info.height;
        depthInfo.mipLevels = 1;
        depthInfo.usage = TextureUsage::DepthStencil | TextureUsage::Sampled;
        
        TextureHandle depthTex = createTexture(depthInfo);
        if (!depthTex.isValid()) {
            LOG_ERROR("GHI_Vulkan", "Failed to create depth attachment");
            for (auto& tex : rt.colorTextures) {
                destroyTexture(tex);
            }
            return RenderTargetHandle{};
        }
        rt.depthTexture = depthTex;
        
        auto viewIt = imageViews_.find(depthTex.id);
        if (viewIt != imageViews_.end()) {
            framebufferViews.push_back(viewIt->second);
        }
        
        VkAttachmentDescription depthAttachDesc{};
        depthAttachDesc.format = convertFormat(info.depthFormat);
        depthAttachDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachDesc.loadOp = info.clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        attachments.push_back(depthAttachDesc);
        
        depthRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        
        VkClearValue depthClear{};
        depthClear.depthStencil = {info.depthClearValue, 0};
        rt.clearValues.push_back(depthClear);
    }
    
    // Create render pass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = info.hasDepth ? &depthRef : nullptr;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &rt.renderPass) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create render pass for render target");
        for (auto& tex : rt.colorTextures) {
            destroyTexture(tex);
        }
        if (rt.depthTexture.isValid()) {
            destroyTexture(rt.depthTexture);
        }
        return RenderTargetHandle{};
    }
    
    // Create framebuffer
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = rt.renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(framebufferViews.size());
    framebufferInfo.pAttachments = framebufferViews.data();
    framebufferInfo.width = info.width;
    framebufferInfo.height = info.height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &rt.framebuffer) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create framebuffer for render target");
        vkDestroyRenderPass(device_, rt.renderPass, nullptr);
        for (auto& tex : rt.colorTextures) {
            destroyTexture(tex);
        }
        if (rt.depthTexture.isValid()) {
            destroyTexture(rt.depthTexture);
        }
        return RenderTargetHandle{};
    }
    
    RenderTargetHandle handle;
    handle.id = nextRenderTargetID_++;
    renderTargets_[handle.id] = std::move(rt);
    
    LOG_INFO("GHI_Vulkan", "Created render target: id=%u, size=%ux%u, colors=%zu, depth=%s",
             handle.id, info.width, info.height, info.colorAttachments.size(),
             info.hasDepth ? "yes" : "no");
    
    return handle;
}

void GHI_VulkanBackend::destroyRenderTarget(RenderTargetHandle handle) {
    if (!handle.isValid()) return;
    
    auto it = renderTargets_.find(handle.id);
    if (it == renderTargets_.end()) return;
    
    RenderTargetData& rt = it->second;
    
    // Wait for GPU
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
    
    // Destroy framebuffer
    if (rt.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, rt.framebuffer, nullptr);
    }
    
    // Destroy render pass
    if (rt.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, rt.renderPass, nullptr);
    }
    
    // Destroy textures
    for (auto& tex : rt.colorTextures) {
        destroyTexture(tex);
    }
    if (rt.depthTexture.isValid()) {
        destroyTexture(rt.depthTexture);
    }
    
    renderTargets_.erase(it);
    
    LOG_INFO("GHI_Vulkan", "Destroyed render target: id=%u", handle.id);
}

TextureHandle GHI_VulkanBackend::getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) {
    if (!target.isValid()) return TextureHandle{};
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return TextureHandle{};
    
    if (index >= it->second.colorTextures.size()) {
        LOG_ERROR("GHI_Vulkan", "Color attachment index out of range: %u", index);
        return TextureHandle{};
    }
    
    return it->second.colorTextures[index];
}

TextureHandle GHI_VulkanBackend::getRenderTargetDepthTexture(RenderTargetHandle target) {
    if (!target.isValid()) return TextureHandle{};
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return TextureHandle{};
    
    return it->second.depthTexture;
}

void GHI_VulkanBackend::resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) {
    if (!target.isValid()) return;
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return;
    
    // For now, just log - full resize would need to recreate all resources
    LOG_WARN("GHI_Vulkan", "Render target resize not yet implemented: id=%u, %ux%u -> %ux%u",
             target.id, it->second.width, it->second.height, width, height);
    
    // TODO: Implement full resize by storing creation info and recreating
}

void GHI_VulkanBackend::setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    // Use negative height viewport to flip Y axis (VK_KHR_maintenance1)
    // This makes Vulkan's coordinate system match OpenGL/Metal without affecting winding
    VkViewport viewport = {};
    viewport.x = static_cast<float>(x);
    viewport.y = static_cast<float>(height);  // Start at bottom
    viewport.width = static_cast<float>(width);
    viewport.height = -static_cast<float>(height);  // Negative = flip Y
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    vkCmdSetViewport(currentCommandBuffer_, 0, 1, &viewport);
}

void GHI_VulkanBackend::setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    VkRect2D scissor = {};
    scissor.offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
    scissor.extent = {width, height};
    
    vkCmdSetScissor(currentCommandBuffer_, 0, 1, &scissor);
}

void GHI_VulkanBackend::draw(uint32_t vertexCount, uint32_t instanceCount, 
                            uint32_t firstVertex, uint32_t firstInstance) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "No active command buffer");
        return;
    }
    
    vkCmdDraw(currentCommandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void GHI_VulkanBackend::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "No active command buffer for drawIndexed");
        return;
    }
    
    // Debug: Log draw calls (first few only to avoid spam)
    static int drawCount = 0;
    if (drawCount < 10) {
        LOG_INFO("GHI_Vulkan", "drawIndexed: %u indices, pipeline bound: %s", 
                 indexCount, currentState_.shader.isValid() ? "yes" : "NO");
        drawCount++;
    }
    
    vkCmdDrawIndexed(currentCommandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void GHI_VulkanBackend::drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "drawIndirect: No active command buffer");
        return;
    }
    
    if (!indirectBuffer.isValid()) {
        LOG_ERROR("GHI_Vulkan", "drawIndirect: Invalid indirect buffer handle");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "drawIndirect: Buffer not found: id=%u", indirectBuffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    
    // Use the correct stride or default to VkDrawIndirectCommand size
    uint32_t actualStride = stride > 0 ? stride : sizeof(VkDrawIndirectCommand);
    
    vkCmdDrawIndirect(currentCommandBuffer_, vkBuffer, 0, drawCount, actualStride);
    
    LOG_INFO("GHI_Vulkan", "drawIndirect: %u draws, stride=%u", drawCount, actualStride);
}

void GHI_VulkanBackend::drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "drawIndexedIndirect: No active command buffer");
        return;
    }
    
    if (!indirectBuffer.isValid()) {
        LOG_ERROR("GHI_Vulkan", "drawIndexedIndirect: Invalid indirect buffer handle");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "drawIndexedIndirect: Buffer not found: id=%u", indirectBuffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    
    // Use the correct stride or default to VkDrawIndexedIndirectCommand size
    uint32_t actualStride = stride > 0 ? stride : sizeof(VkDrawIndexedIndirectCommand);
    
    vkCmdDrawIndexedIndirect(currentCommandBuffer_, vkBuffer, 0, drawCount, actualStride);
    
    LOG_INFO("GHI_Vulkan", "drawIndexedIndirect: %u draws, stride=%u", drawCount, actualStride);
}

ShaderHandle GHI_VulkanBackend::createComputeShader(const ShaderSource& source) {
    if (!source.computePath && !source.computeSource) {
        LOG_ERROR("GHI_Vulkan", "No compute shader source provided");
        return ShaderHandle{};
    }
    
    // Load SPIR-V code
    std::vector<uint32_t> computeCode;
    if (source.computePath) {
        if (!loadSPIRV(source.computePath, computeCode)) {
            LOG_ERROR("GHI_Vulkan", "Failed to load compute shader: %s", source.computePath);
            return ShaderHandle{};
        }
        LOG_INFO("GHI_Vulkan", "Loaded compute SPIR-V: %s (%zu bytes)", 
                 source.computePath, computeCode.size() * 4);
    }
    
    // Create shader module
    VkShaderModule computeModule = createShaderModule(computeCode);
    if (computeModule == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "Failed to create compute shader module");
        return ShaderHandle{};
    }
    
    // Create compute pipeline layout (using same descriptor set layouts as graphics)
    VkDescriptorSetLayout setLayouts[] = {descriptorSetLayout0_, descriptorSetLayout1_};
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    
    VkPipelineLayout computePipelineLayout;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create compute pipeline layout");
        vkDestroyShaderModule(device_, computeModule, nullptr);
        return ShaderHandle{};
    }
    
    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = computePipelineLayout;
    
    VkPipeline computePipeline;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create compute pipeline");
        vkDestroyPipelineLayout(device_, computePipelineLayout, nullptr);
        vkDestroyShaderModule(device_, computeModule, nullptr);
        return ShaderHandle{};
    }
    
    // Cleanup shader module (pipeline retains what it needs)
    vkDestroyShaderModule(device_, computeModule, nullptr);
    
    // Store handles
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    computePipelines_[handle.id] = computePipeline;
    pipelineLayouts_[handle.id] = computePipelineLayout;
    
    LOG_INFO("GHI_Vulkan", "Created compute shader pipeline: id=%u", handle.id);
    return handle;
}

void GHI_VulkanBackend::bindComputeShader(ShaderHandle shader) {
    if (!shader.isValid()) {
        LOG_ERROR("GHI_Vulkan", "Cannot bind invalid compute shader");
        return;
    }
    
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "Cannot bind compute shader: no active command buffer");
        return;
    }
    
    auto it = computePipelines_.find(shader.id);
    if (it == computePipelines_.end() || it->second == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "Compute pipeline not found: id=%u", shader.id);
        return;
    }
    
    vkCmdBindPipeline(currentCommandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, it->second);
    
    // Bind descriptor sets for compute
    auto layoutIt = pipelineLayouts_.find(shader.id);
    if (layoutIt != pipelineLayouts_.end() && !descriptorSets_.empty()) {
        vkCmdBindDescriptorSets(currentCommandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                                layoutIt->second, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
    }
    
    LOG_INFO("GHI_Vulkan", "Bound compute shader: id=%u", shader.id);
}

void GHI_VulkanBackend::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    vkCmdDispatch(currentCommandBuffer_, groupCountX, groupCountY, groupCountZ);
}

void GHI_VulkanBackend::dispatchIndirect(BufferHandle indirectBuffer) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_ERROR("GHI_Vulkan", "dispatchIndirect: No active command buffer");
        return;
    }
    
    if (!indirectBuffer.isValid()) {
        LOG_ERROR("GHI_Vulkan", "dispatchIndirect: Invalid indirect buffer handle");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "dispatchIndirect: Buffer not found: id=%u", indirectBuffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    
    vkCmdDispatchIndirect(currentCommandBuffer_, vkBuffer, 0);
    
    LOG_INFO("GHI_Vulkan", "dispatchIndirect: buffer=%u", indirectBuffer.id);
}

void GHI_VulkanBackend::setRenderState(const RenderState& state) {
    currentState_ = state;
    
    // Bind pipeline if we have one
    if (currentCommandBuffer_ != VK_NULL_HANDLE && state.shader.isValid()) {
        PipelineKey key;
        key.shaderId = state.shader.id;
        key.blendEnabled = state.blendEnabled;
        key.srcColorBlendFactor = state.srcColorBlendFactor;
        key.dstColorBlendFactor = state.dstColorBlendFactor;
        key.colorBlendOp = state.colorBlendOp;
        key.srcAlphaBlendFactor = state.srcAlphaBlendFactor;
        key.dstAlphaBlendFactor = state.dstAlphaBlendFactor;
        key.alphaBlendOp = state.alphaBlendOp;

        auto it = pipelineCache_.find(key);
        VkPipeline pipeline = VK_NULL_HANDLE;

        if (it != pipelineCache_.end()) {
            pipeline = it->second;
        } else {
            // Create new pipeline variant
            auto moduleIt = shaderModules_.find(state.shader.id);
            if (moduleIt != shaderModules_.end()) {
                pipeline = createGraphicsPipeline(moduleIt->second.vertModule, 
                                                 moduleIt->second.fragModule, 
                                                 state, 
                                                 state.shader.id);
                if (pipeline != VK_NULL_HANDLE) {
                    pipelineCache_[key] = pipeline;
                    LOG_INFO("GHI_Vulkan", "Created and cached pipeline variant for shader %u", state.shader.id);
                }
            } else {
                LOG_ERROR("GHI_Vulkan", "Shader modules not found for shader id=%u", state.shader.id);
            }
        }

        if (pipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(currentCommandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        }
    }
}

void GHI_VulkanBackend::getRenderState(RenderState& state) {
    state = currentState_;
}

void GHI_VulkanBackend::bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid buffer handle: %u", buffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(currentCommandBuffer_, binding, 1, &vkBuffer, &vkOffset);
}

void GHI_VulkanBackend::bindIndexBuffer(BufferHandle buffer, size_t offset) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid buffer handle: %u", buffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    vkCmdBindIndexBuffer(currentCommandBuffer_, vkBuffer, offset, VK_INDEX_TYPE_UINT16);  // Match Metal (uint16)
}

void GHI_VulkanBackend::bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid buffer handle: %u", buffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    
    // Get the descriptor set for current frame + set index
    uint32_t descriptorIndex = currentFrame_ * 2 + set;
    if (descriptorIndex >= descriptorSets_.size()) {
        LOG_ERROR("GHI_Vulkan", "Invalid descriptor set index: %u", descriptorIndex);
        return;
    }
    
    VkDescriptorSet descriptorSet = descriptorSets_[descriptorIndex];
    
    // Update descriptor set with this buffer
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;
    
    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    
    // Bind the descriptor set (if we have a pipeline bound)
    if (currentState_.shader.isValid()) {
        auto layoutIt = pipelineLayouts_.find(currentState_.shader.id);
        if (layoutIt != pipelineLayouts_.end()) {
            vkCmdBindDescriptorSets(
                currentCommandBuffer_,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                layoutIt->second,
                set,
                1,
                &descriptorSet,
                0, nullptr
            );
            
            // Debug log first binding
            static int bindCount = 0;
            if (bindCount < 5) {
                LOG_INFO("GHI_Vulkan", "Bound descriptor set %u (binding %u)", set, binding);
                bindCount++;
            }
        } else {
            LOG_ERROR("GHI_Vulkan", "Pipeline layout not found for shader %u", currentState_.shader.id);
        }
    } else {
        LOG_WARN("GHI_Vulkan", "bindUniformBuffer: no shader bound (set=%u, binding=%u)", set, binding);
    }
}

void GHI_VulkanBackend::bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    if (!texture.isValid()) {
        LOG_WARN("GHI_Vulkan", "bindTexture: Invalid texture handle");
        return;
    }
    
    auto viewIt = imageViews_.find(texture.id);
    auto samplerIt = samplers_.find(texture.id);
    
    if (viewIt == imageViews_.end() || samplerIt == samplers_.end()) {
        LOG_ERROR("GHI_Vulkan", "bindTexture: Texture not found: id=%u", texture.id);
        return;
    }
    
    VkImageView imageView = viewIt->second;
    VkSampler sampler = samplerIt->second;
    
    // Get descriptor set for the current frame and set index
    uint32_t descriptorIndex = currentFrame_ * 2 + set;
    if (descriptorIndex >= descriptorSets_.size()) {
        LOG_ERROR("GHI_Vulkan", "Invalid descriptor set index for texture: %u", descriptorIndex);
        return;
    }
    
    VkDescriptorSet descriptorSet = descriptorSets_[descriptorIndex];
    
    // Update descriptor set with combined image sampler
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;
    
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    
    // Bind the descriptor set if we have a pipeline bound
    if (currentState_.shader.isValid()) {
        auto layoutIt = pipelineLayouts_.find(currentState_.shader.id);
        if (layoutIt != pipelineLayouts_.end()) {
            vkCmdBindDescriptorSets(
                currentCommandBuffer_,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                layoutIt->second,
                set,
                1,
                &descriptorSet,
                0, nullptr
            );
        }
    }
    
    // Debug log first texture binding
    static int textureBindCount = 0;
    if (textureBindCount < 3) {
        LOG_INFO("GHI_Vulkan", "Bound texture: id=%u to set=%u binding=%u", texture.id, set, binding);
        textureBindCount++;
    }
}

void GHI_VulkanBackend::bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid storage buffer handle: %u", buffer.id);
        return;
    }
    
    VkBuffer vkBuffer = it->second;
    
    // Get descriptor set for the current frame and set index
    uint32_t descriptorIndex = currentFrame_ * 2 + set;
    if (descriptorIndex >= descriptorSets_.size()) {
        LOG_ERROR("GHI_Vulkan", "Invalid descriptor set index for storage buffer: %u", descriptorIndex);
        return;
    }
    
    // Update descriptor with storage buffer
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = vkBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;
    
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSets_[descriptorIndex];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void GHI_VulkanBackend::bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    auto viewIt = imageViews_.find(texture.id);
    if (viewIt == imageViews_.end()) {
        LOG_ERROR("GHI_Vulkan", "Invalid storage texture handle: %u", texture.id);
        return;
    }
    
    VkImageView imageView = viewIt->second;
    
    // Get descriptor set for the current frame and set index
    uint32_t descriptorIndex = currentFrame_ * 2 + set;
    if (descriptorIndex >= descriptorSets_.size()) {
        LOG_ERROR("GHI_Vulkan", "Invalid descriptor set index for storage texture: %u", descriptorIndex);
        return;
    }
    
    // Update descriptor with storage image
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;  // Storage images use GENERAL layout
    
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSets_[descriptorIndex];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void GHI_VulkanBackend::setPushConstants(const void* data, uint32_t size, uint32_t offset) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) {
        LOG_WARN("GHI_Vulkan", "setPushConstants: no command buffer");
        return;
    }
    if (!currentState_.shader.isValid()) {
        LOG_WARN("GHI_Vulkan", "setPushConstants: no shader bound");
        return;
    }
    
    auto layoutIt = pipelineLayouts_.find(currentState_.shader.id);
    if (layoutIt == pipelineLayouts_.end()) {
        LOG_ERROR("GHI_Vulkan", "Pipeline layout not found for shader %u", currentState_.shader.id);
        return;
    }
    
    vkCmdPushConstants(
        currentCommandBuffer_,
        layoutIt->second,
        VK_SHADER_STAGE_VERTEX_BIT,
        offset,
        size,
        data
    );
    
    // Log first push constant call only
    static bool loggedOnce = false;
    if (!loggedOnce) {
        LOG_INFO("GHI_Vulkan", "Push constants set: %u bytes at offset %u", size, offset);
        loggedOnce = true;
    }
}

void GHI_VulkanBackend::memoryBarrier() {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(
        currentCommandBuffer_,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr
    );
}

void GHI_VulkanBackend::bufferBarrier(BufferHandle buffer) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.buffer = it->second;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(
        currentCommandBuffer_,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr
    );
}

void GHI_VulkanBackend::textureBarrier(TextureHandle texture) {
    if (currentCommandBuffer_ == VK_NULL_HANDLE) return;
    
    // TODO: Image memory barrier
}

VkFormat GHI_VulkanBackend::convertFormat(Format format) {
    switch (format) {
        case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
        case Format::RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::Depth32F: return VK_FORMAT_D32_SFLOAT;
        case Format::Depth24_Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// ============================================================================
// Helper Methods for Standalone Initialization
// ============================================================================

bool GHI_VulkanBackend::createInstance() {
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
    
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = 0;
    
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create Vulkan instance (error: %d)", result);
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created Vulkan instance");
    return true;
}

bool GHI_VulkanBackend::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        LOG_ERROR("GHI_Vulkan", "No GPUs with Vulkan support found");
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    
    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            queueFamilies_ = findQueueFamilies(device);
            
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            LOG_INFO("GHI_Vulkan", "Selected GPU: %s", props.deviceName);
            return true;
        }
    }
    
    LOG_ERROR("GHI_Vulkan", "Failed to find suitable GPU");
    return false;
}

bool GHI_VulkanBackend::isDeviceSuitable(VkPhysicalDevice device) {
    GHI_VulkanBackend::QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    return indices.graphicsFamily != UINT32_MAX && extensionsSupported;
}

bool GHI_VulkanBackend::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    
    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
    };
    
    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());
    
    for (const auto& extension : availableExtensions) {
        required.erase(extension.extensionName);
    }
    
    return required.empty();
}

GHI_VulkanBackend::QueueFamilyIndices GHI_VulkanBackend::findQueueFamilies(VkPhysicalDevice device) {
    GHI_VulkanBackend::QueueFamilyIndices indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
            
            if (surface_ != VK_NULL_HANDLE) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
                if (presentSupport) {
                    indices.presentFamily = i;
                }
            } else {
                indices.presentFamily = i;
            }
        }
        
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.computeFamily = i;
        }
        
        if (indices.isComplete()) break;
    }
    
    return indices;
}

bool GHI_VulkanBackend::createLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {queueFamilies_.graphicsFamily};
    if (queueFamilies_.computeFamily != UINT32_MAX) {
        uniqueQueueFamilies.insert(queueFamilies_.computeFamily);
    }
    
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
    
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
    };
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;
    
    VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create logical device (error: %d)", result);
        return false;
    }
    
    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily, 0, &presentQueue_);
    if (queueFamilies_.computeFamily != UINT32_MAX) {
        vkGetDeviceQueue(device_, queueFamilies_.computeFamily, 0, &computeQueue_);
    }
    
    LOG_INFO("GHI_Vulkan", "Created logical device and queues");
    return true;
}

bool GHI_VulkanBackend::createAllocator() {
    // VMA needs Vulkan function pointers (borrowed from HelloVulkan)
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.instance = instance_;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    
    VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create VMA allocator (error: %d)", result);
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created VMA allocator");
    return true;
}

bool GHI_VulkanBackend::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilies_.graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    VkResult result = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create command pool (error: %d)", result);
        return false;
    }
    
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

bool GHI_VulkanBackend::createSyncObjects() {
    imageAvailableSemaphores_.resize(2);
    renderFinishedSemaphores_.resize(2);
    inFlightFences_.resize(2);
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (size_t i = 0; i < 2; i++) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            LOG_ERROR("GHI_Vulkan", "Failed to create sync objects");
            return false;
        }
    }
    
    LOG_INFO("GHI_Vulkan", "Created sync objects");
    return true;
}

// ============================================================================
// Surface & Swapchain (Borrowed from Jupiter vulkan_backend.cpp)
// ============================================================================

void GHI_VulkanBackend::setSurface(VkSurfaceKHR surface, uint32_t width, uint32_t height) {
    surface_ = surface;
    
    if (!createSwapchain(width, height)) {
        LOG_ERROR("GHI_Vulkan", "Failed to create swapchain");
        return;
    }
    
    if (!createDepthResources()) {
        LOG_ERROR("GHI_Vulkan", "Failed to create depth resources");
        return;
    }
    
    if (!createRenderPass()) {
        LOG_ERROR("GHI_Vulkan", "Failed to create render pass");
        return;
    }
    
    if (!createFramebuffers()) {
        LOG_ERROR("GHI_Vulkan", "Failed to create framebuffers");
        return;
    }
    
    LOG_INFO("GHI_Vulkan", "Vulkan surface configured with swapchain");
}

bool GHI_VulkanBackend::createSwapchain(uint32_t width, uint32_t height) {
    // Query swapchain support
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);
    
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
    
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());
    
    // Choose format (prefer BGRA8 SRGB)
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
    }
    
    // Choose present mode (prefer mailbox for lower latency)
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;  // Always available
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = mode;
            break;
        }
    }
    
    // Choose extent
    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        extent = {width, height};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    
    swapchainImageFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;
    
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
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
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create swapchain");
        return false;
    }
    
    // Get swapchain images
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());
    
    // Create image views
    swapchainImageViews_.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device_, &viewInfo, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) {
            LOG_ERROR("GHI_Vulkan", "Failed to create image view %zu", i);
            return false;
        }
    }
    
    LOG_INFO("GHI_Vulkan", "Created swapchain with %u images (%ux%u)",
             imageCount, extent.width, extent.height);
    return true;
}

bool GHI_VulkanBackend::createDepthResources() {
    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat_;
    imageInfo.extent.width = swapchainExtent_.width;
    imageInfo.extent.height = swapchainExtent_.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &depthImage_, &depthImageAllocation_, nullptr) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create depth image");
        return false;
    }
    
    // Create depth image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create depth image view");
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created depth buffer: %ux%u", swapchainExtent_.width, swapchainExtent_.height);
    return true;
}

bool GHI_VulkanBackend::createRenderPass() {
    // Color attachment (attachment 0)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    // Depth attachment (attachment 1)
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Don't need to store depth
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    
    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create render pass");
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created render pass with depth attachment");
    return true;
}

bool GHI_VulkanBackend::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    
    for (size_t i = 0; i < swapchainImageViews_.size(); i++) {
        // Color attachment + depth attachment
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews_[i],
            depthImageView_
        };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent_.width;
        framebufferInfo.height = swapchainExtent_.height;
        framebufferInfo.layers = 1;
        
        if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            LOG_ERROR("GHI_Vulkan", "Failed to create framebuffer %zu", i);
            return false;
        }
    }
    
    LOG_INFO("GHI_Vulkan", "Created %zu framebuffers with depth", framebuffers_.size());
    return true;
}

// ============================================================================
// Shader Loading Helpers
// ============================================================================

bool GHI_VulkanBackend::loadSPIRV(const char* filename, std::vector<uint32_t>& code) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        LOG_ERROR("GHI_Vulkan", "Failed to open SPIR-V file: %s", filename);
        return false;
    }
    
    size_t fileSize = (size_t)file.tellg();
    code.resize(fileSize / sizeof(uint32_t));
    
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);
    file.close();
    
    LOG_INFO("GHI_Vulkan", "Loaded SPIR-V: %s (%zu bytes)", filename, fileSize);
    return true;
}

VkShaderModule GHI_VulkanBackend::createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    
    return shaderModule;
}

VkPipeline GHI_VulkanBackend::createGraphicsPipeline(VkShaderModule vertModule, VkShaderModule fragModule, const RenderState& state, uint32_t shaderId) {
    // Shader stages
    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};
    
    // Vertex input (matches primitives::Vertex)
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(float) * 8;  // 32 bytes
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attributeDescs[3];
    attributeDescs[0].binding = 0;
    attributeDescs[0].location = 0;
    attributeDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[0].offset = 0;
    
    attributeDescs[1].binding = 0;
    attributeDescs[1].location = 1;
    attributeDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescs[1].offset = 12;
    
    attributeDescs[2].binding = 0;
    attributeDescs[2].location = 2;
    attributeDescs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescs[2].offset = 24;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs;
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Viewport (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Dynamic state (viewport and scissor can be changed at draw time)
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = convertCullMode(state.cullMode);
    rasterizer.frontFace = convertFrontFace(state.frontFace);
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = state.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = state.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = convertCompareOp(state.depthCompareOp);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = state.blendEnabled ? VK_TRUE : VK_FALSE;
    if (state.blendEnabled) {
        colorBlendAttachment.srcColorBlendFactor = convertBlendFactor(state.srcColorBlendFactor);
        colorBlendAttachment.dstColorBlendFactor = convertBlendFactor(state.dstColorBlendFactor);
        colorBlendAttachment.colorBlendOp = convertBlendOp(state.colorBlendOp);
        colorBlendAttachment.srcAlphaBlendFactor = convertBlendFactor(state.srcAlphaBlendFactor);
        colorBlendAttachment.dstAlphaBlendFactor = convertBlendFactor(state.dstAlphaBlendFactor);
        colorBlendAttachment.alphaBlendOp = convertBlendOp(state.alphaBlendOp);
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // Pipeline layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    auto layoutIt = pipelineLayouts_.find(shaderId);
    if (layoutIt != pipelineLayouts_.end()) {
        pipelineLayout = layoutIt->second;
    } else {
        VkDescriptorSetLayout setLayouts[] = {descriptorSetLayout0_, descriptorSetLayout1_};
        
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 96;
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 2;
        pipelineLayoutInfo.pSetLayouts = setLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        
        if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            LOG_ERROR("GHI_Vulkan", "Failed to create pipeline layout");
            return VK_NULL_HANDLE;
        }
        pipelineLayouts_[shaderId] = pipelineLayout;
    }
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    
    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create graphics pipeline variant");
        return VK_NULL_HANDLE;
    }
    
    return pipeline;
}

bool GHI_VulkanBackend::createDescriptorSetLayouts() {
    // Set 0: Camera (binding 0) + Object (binding 1)
    VkDescriptorSetLayoutBinding bindings0[2];
    
    // Camera uniform
    bindings0[0].binding = 0;
    bindings0[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings0[0].descriptorCount = 1;
    bindings0[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings0[0].pImmutableSamplers = nullptr;
    
    // Object uniform
    bindings0[1].binding = 1;
    bindings0[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings0[1].descriptorCount = 1;
    bindings0[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings0[1].pImmutableSamplers = nullptr;
    
    // MoltenVK workaround: Add binding flags (even if zero) to avoid MSL conversion error
    VkDescriptorBindingFlags bindingFlags0[2] = {0, 0};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo0{};
    bindingFlagsInfo0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo0.bindingCount = 2;
    bindingFlagsInfo0.pBindingFlags = bindingFlags0;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo0{};
    layoutInfo0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo0.bindingCount = 2;
    layoutInfo0.pBindings = bindings0;
    layoutInfo0.pNext = &bindingFlagsInfo0;  // MoltenVK fix
    
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo0, nullptr, &descriptorSetLayout0_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create descriptor set layout 0");
        return false;
    }
    
    // Set 0 needs texture at binding 2
    // Recreate set 0 with camera (0), object (1), texture (2)
    VkDescriptorSetLayoutBinding bindings0Full[3];
    bindings0Full[0] = bindings0[0];  // Camera
    bindings0Full[1] = bindings0[1];  // Object
    
    // Texture
    bindings0Full[2].binding = 2;
    bindings0Full[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings0Full[2].descriptorCount = 1;
    bindings0Full[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings0Full[2].pImmutableSamplers = nullptr;
    
    // Destroy and recreate set 0 with texture
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout0_, nullptr);
    
    VkDescriptorBindingFlags bindingFlags0Full[3] = {0, 0, 0};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo0Full{};
    bindingFlagsInfo0Full.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo0Full.bindingCount = 3;
    bindingFlagsInfo0Full.pBindingFlags = bindingFlags0Full;
    
    layoutInfo0.bindingCount = 3;
    layoutInfo0.pBindings = bindings0Full;
    layoutInfo0.pNext = &bindingFlagsInfo0Full;
    
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo0, nullptr, &descriptorSetLayout0_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to recreate descriptor set layout 0 with texture");
        return false;
    }
    
    // Set 1: Lighting (binding 0) + Material (binding 1)
    VkDescriptorSetLayoutBinding bindings1[2];
    
    // Lighting uniform
    bindings1[0].binding = 0;
    bindings1[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings1[0].descriptorCount = 1;
    bindings1[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings1[0].pImmutableSamplers = nullptr;
    
    // Material uniform
    bindings1[1].binding = 1;
    bindings1[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings1[1].descriptorCount = 1;
    bindings1[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings1[1].pImmutableSamplers = nullptr;
    
    // MoltenVK workaround for set 1
    VkDescriptorBindingFlags bindingFlags1[2] = {0, 0};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo1{};
    bindingFlagsInfo1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo1.bindingCount = 2;
    bindingFlagsInfo1.pBindingFlags = bindingFlags1;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo1{};
    layoutInfo1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo1.bindingCount = 2;
    layoutInfo1.pBindings = bindings1;
    layoutInfo1.pNext = &bindingFlagsInfo1;  // MoltenVK fix
    
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo1, nullptr, &descriptorSetLayout1_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create descriptor set layout 1");
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout0_, nullptr);
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created descriptor set layouts");
    return true;
}

bool GHI_VulkanBackend::createDescriptorPool() {
    // Create pool with enough descriptors for our needs
    VkDescriptorPoolSize poolSizes[2];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 20;  // Plenty for camera, object, lighting, material buffers
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 10;  // Textures
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 10;  // Max descriptor sets
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        LOG_ERROR("GHI_Vulkan", "Failed to create descriptor pool");
        return false;
    }
    
    LOG_INFO("GHI_Vulkan", "Created descriptor pool");
    return true;
}

bool GHI_VulkanBackend::allocateDescriptorSets() {
    // Allocate 2 descriptor sets per frame (set 0 and set 1)
    descriptorSets_.resize(4);  // 2 frames × 2 sets
    
    VkDescriptorSetLayout layouts[] = {descriptorSetLayout0_, descriptorSetLayout1_};
    
    for (int frame = 0; frame < 2; frame++) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = 2;
        allocInfo.pSetLayouts = layouts;
        
        VkDescriptorSet sets[2];
        if (vkAllocateDescriptorSets(device_, &allocInfo, sets) != VK_SUCCESS) {
            LOG_ERROR("GHI_Vulkan", "Failed to allocate descriptor sets for frame %d", frame);
            return false;
        }
        
        descriptorSets_[frame * 2 + 0] = sets[0];  // Set 0
        descriptorSets_[frame * 2 + 1] = sets[1];  // Set 1
    }
    
    LOG_INFO("GHI_Vulkan", "Allocated descriptor sets");
    return true;
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter

