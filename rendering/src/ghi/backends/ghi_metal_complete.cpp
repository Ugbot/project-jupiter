/**
 * @file ghi_metal_complete.cpp
 * @brief Complete Metal backend implementation using metal-cpp
 * 
 * Full implementation of GHI_MetalBackend using metal-cpp C++ wrapper.
 */

// Define metal-cpp implementation
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "ghi_metal.h"
#include "../util/ghi_shader_cross.h"
#include "logging/logging.h"
#include <cstring>
#include <string>
#include <fstream>

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Constructor / Destructor
// ============================================================================

GHI_MetalBackend::GHI_MetalBackend() {
    LOG_INFO("GHI_Metal", "Metal backend created (metal-cpp C++)");
}

GHI_MetalBackend::~GHI_MetalBackend() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool GHI_MetalBackend::initialize() {
    LOG_INFO("GHI_Metal", "Initializing Metal backend (metal-cpp C++)");
    
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    // Create Metal device
    MTL::Device* mtlDevice = MTL::CreateSystemDefaultDevice();
    if (!mtlDevice) {
        LOG_ERROR("GHI_Metal", "Failed to create Metal device");
        pPool->release();
        return false;
    }
    
    device_ = mtlDevice;
    
    // Log device name immediately while pool is alive
    NS::String* deviceName = mtlDevice->name();
    if (deviceName) {
        const char* utf8 = deviceName->utf8String();
        if (utf8) {
            LOG_INFO("GHI_Metal", "Metal device: %s", utf8);
        }
    }
    
    // Create command queue
    MTL::CommandQueue* mtlQueue = mtlDevice->newCommandQueue();
    if (!mtlQueue) {
        LOG_ERROR("GHI_Metal", "Failed to create command queue");
        mtlDevice->release();
        pPool->release();
        return false;
    }
    
    commandQueue_ = mtlQueue;
    
    // Create depth stencil state for 3D rendering
    MTL::DepthStencilDescriptor* depthStateDesc = MTL::DepthStencilDescriptor::alloc()->init();
    depthStateDesc->setDepthCompareFunction(MTL::CompareFunctionLess);
    depthStateDesc->setDepthWriteEnabled(true);
    depthStencilState_ = mtlDevice->newDepthStencilState(depthStateDesc);
    depthStateDesc->release();
    
    if (!depthStencilState_) {
        LOG_ERROR("GHI_Metal", "Failed to create depth stencil state");
    }
    
    // Query capabilities (will copy strings)
    queryCapabilities();
    
    LOG_INFO("GHI_Metal", "Metal backend initialized successfully");
    
    pPool->release();
    return true;
}

void GHI_MetalBackend::shutdown() {
    LOG_INFO("GHI_Metal", "Shutting down Metal backend");
    
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    // Release resources
    for (auto& [id, buffer] : buffers_) {
        if (buffer) {
            MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(buffer);
            mtlBuffer->release();
        }
    }
    buffers_.clear();
    
    for (auto& [id, texData] : textures_) {
        if (texData.texture) {
            MTL::Texture* mtlTexture = static_cast<MTL::Texture*>(texData.texture);
            mtlTexture->release();
        }
        if (texData.sampler) {
            MTL::SamplerState* mtlSampler = static_cast<MTL::SamplerState*>(texData.sampler);
            mtlSampler->release();
        }
    }
    textures_.clear();

    // Release shader data
    for (auto& [id, data] : shaders_) {
        if (data.vertexFunction) static_cast<MTL::Function*>(data.vertexFunction)->release();
        if (data.fragmentFunction) static_cast<MTL::Function*>(data.fragmentFunction)->release();
        if (data.vertexDescriptor) static_cast<MTL::VertexDescriptor*>(data.vertexDescriptor)->release();
    }
    shaders_.clear();

    // Release pipeline cache
    for (auto& [key, pipeline] : pipelineCache_) {
        if (pipeline) static_cast<MTL::RenderPipelineState*>(pipeline)->release();
    }
    pipelineCache_.clear();

    // Release depth stencil state cache
    for (auto& [key, state] : depthStencilStateCache_) {
        if (state) static_cast<MTL::DepthStencilState*>(state)->release();
    }
    depthStencilStateCache_.clear();
    
    // Release depth resources
    if (depthTexture_) {
        MTL::Texture* depthTex = static_cast<MTL::Texture*>(depthTexture_);
        depthTex->release();
        depthTexture_ = nullptr;
    }
    
    if (depthStencilState_) {
        MTL::DepthStencilState* depthState = static_cast<MTL::DepthStencilState*>(depthStencilState_);
        depthState->release();
        depthStencilState_ = nullptr;
    }
    
    if (commandQueue_) {
        MTL::CommandQueue* mtlQueue = static_cast<MTL::CommandQueue*>(commandQueue_);
        mtlQueue->release();
        commandQueue_ = nullptr;
    }
    
    if (device_) {
        MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
        mtlDevice->release();
        device_ = nullptr;
    }
    
    pPool->release();
}

void GHI_MetalBackend::waitIdle() {
    // Metal handles synchronization automatically
}

void GHI_MetalBackend::queryCapabilities() {
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    capabilities_.backend = Backend::Metal;
    NS::String* name = mtlDevice->name();
    // Copy the string to avoid use-after-free when autorelease pool drains
    if (name) {
        const char* utf8 = name->utf8String();
        if (utf8) {
            capabilities_.deviceName = std::string(utf8);
        } else {
            capabilities_.deviceName = "Metal Device";
        }
    } else {
        capabilities_.deviceName = "Metal Device";
    }
    
    capabilities_.hasComputeShaders = true;
    capabilities_.hasIndirectDraw = true;
    capabilities_.hasStorageBuffers = true;
    capabilities_.hasSubgroups = true;
    capabilities_.subgroupSize = 32;
    capabilities_.hasTileShaders = mtlDevice->supportsFamily(MTL::GPUFamilyApple4);
    capabilities_.hasMemorylessTextures = true;
}

// ============================================================================
// Resource Creation
// ============================================================================

BufferHandle GHI_MetalBackend::createBuffer(const BufferCreateInfo& info) {
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    MTL::ResourceOptions options = MTL::ResourceStorageModeShared;
    
    MTL::Buffer* mtlBuffer = nullptr;
    if (info.data && info.size > 0) {
        mtlBuffer = mtlDevice->newBuffer(info.data, info.size, options);
    } else if (info.size > 0) {
        mtlBuffer = mtlDevice->newBuffer(info.size, options);
    } else {
        return BufferHandle{};
    }
    
    if (!mtlBuffer) {
        return BufferHandle{};
    }
    
    BufferHandle handle;
    handle.id = nextBufferID_++;
    buffers_[handle.id] = mtlBuffer;
    
    return handle;
}

void GHI_MetalBackend::destroyBuffer(BufferHandle handle) {
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end()) {
        if (it->second) {
            MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
            mtlBuffer->release();
        }
        buffers_.erase(it);
    }
}

void GHI_MetalBackend::updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
    void* contents = mtlBuffer->contents();
    if (contents) {
        std::memcpy(static_cast<uint8_t*>(contents) + offset, data, size);
    }
}

// ============================================================================
// Frame Management & Rendering  
// (Texture/shader methods are below)

void GHI_MetalBackend::setMetalLayer(void* layer) {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    metalLayer_ = layer;
    CA::MetalLayer* caLayer = static_cast<CA::MetalLayer*>(layer);
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    // Configure layer with sRGB for proper gamma correction
    caLayer->setDevice(mtlDevice);
    caLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
    caLayer->setFramebufferOnly(false);  // Allow reading if needed
    
    LOG_INFO("GHI_Metal", "CAMetalLayer configured");
    
    pPool->release();
}

void GHI_MetalBackend::setDrawableSize(uint32_t width, uint32_t height) {
    if (metalLayer_) {
        CA::MetalLayer* caLayer = static_cast<CA::MetalLayer*>(metalLayer_);
        caLayer->setDrawableSize(CGSize{static_cast<double>(width), static_cast<double>(height)});
    }
    
    // Create or recreate depth texture if size changed
    if (width != depthTextureWidth_ || height != depthTextureHeight_) {
        MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
        if (!mtlDevice) return;
        
        // Release old depth texture if exists
        if (depthTexture_) {
            MTL::Texture* oldDepth = static_cast<MTL::Texture*>(depthTexture_);
            oldDepth->release();
            depthTexture_ = nullptr;
        }
        
        // Create new depth texture
        MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatDepth32Float,
            width, height,
            false  // not mipmapped
        );
        depthDesc->setStorageMode(MTL::StorageModePrivate);
        depthDesc->setUsage(MTL::TextureUsageRenderTarget);
        
        depthTexture_ = mtlDevice->newTexture(depthDesc);
        depthTextureWidth_ = width;
        depthTextureHeight_ = height;
        
        LOG_INFO("GHI_Metal", "Created depth texture: %ux%u", width, height);
    }
}

void GHI_MetalBackend::beginFrame() {
    // Create autorelease pool for the entire frame
    // This keeps autoreleased objects (like drawable) alive until endFrame()
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    frameAutoreleasePool_ = pPool;
    
    MTL::CommandQueue* mtlQueue = static_cast<MTL::CommandQueue*>(commandQueue_);
    if (!mtlQueue) {
        LOG_ERROR("GHI_Metal", "No command queue");
        pPool->release();
        frameAutoreleasePool_ = nullptr;
        return;
    }
    
    CA::MetalLayer* caLayer = static_cast<CA::MetalLayer*>(metalLayer_);
    if (!caLayer) {
        LOG_ERROR("GHI_Metal", "No metal layer set");
        pPool->release();
        frameAutoreleasePool_ = nullptr;
        return;
    }
    
    // Get next drawable - nextDrawable() returns autoreleased object
    // The frameAutoreleasePool keeps it alive until we drain the pool
    CA::MetalDrawable* drawable = caLayer->nextDrawable();
    if (!drawable) {
        LOG_WARN("GHI_Metal", "No drawable available (window minimized?)");
        pPool->release();
        frameAutoreleasePool_ = nullptr;
        return;
    }
    
    currentDrawable_ = drawable;
    
    // Create command buffer for this frame
    MTL::CommandBuffer* cmdBuffer = mtlQueue->commandBuffer();
    if (!cmdBuffer) {
        LOG_ERROR("GHI_Metal", "Failed to create command buffer");
        pPool->release();
        frameAutoreleasePool_ = nullptr;
        return;
    }
    currentCommandBuffer_ = cmdBuffer;
}

void GHI_MetalBackend::endFrame() {
    if (!currentCommandBuffer_) {
        // Still need to drain the pool even if we have no work
        if (frameAutoreleasePool_) {
            NS::AutoreleasePool* pPool = static_cast<NS::AutoreleasePool*>(frameAutoreleasePool_);
            pPool->release();
            frameAutoreleasePool_ = nullptr;
        }
        return;
    }
    
    MTL::CommandBuffer* cmdBuffer = static_cast<MTL::CommandBuffer*>(currentCommandBuffer_);
    
    // Present drawable if we have one
    if (currentDrawable_) {
        CA::MetalDrawable* drawable = static_cast<CA::MetalDrawable*>(currentDrawable_);
        cmdBuffer->presentDrawable(drawable);
        currentDrawable_ = nullptr;
    }
    
    // Commit command buffer (async)
    cmdBuffer->commit();
    
    // For now, wait synchronously to avoid overwhelming the GPU
    // TODO: Use addCompletedHandler for async operation
    cmdBuffer->waitUntilCompleted();
    
    currentCommandBuffer_ = nullptr;
    
    // Drain the frame's autorelease pool LAST
    // This releases the drawable and any other autoreleased objects
    if (frameAutoreleasePool_) {
        NS::AutoreleasePool* pPool = static_cast<NS::AutoreleasePool*>(frameAutoreleasePool_);
        pPool->release();
        frameAutoreleasePool_ = nullptr;
    }
}

void GHI_MetalBackend::beginRenderPass() {
    if (!currentCommandBuffer_ || !currentDrawable_) {
        LOG_WARN("GHI_Metal", "Cannot begin render pass: no command buffer or drawable");
        return;
    }
    
    MTL::CommandBuffer* cmdBuffer = static_cast<MTL::CommandBuffer*>(currentCommandBuffer_);
    CA::MetalDrawable* drawable = static_cast<CA::MetalDrawable*>(currentDrawable_);
    
    // Create render pass descriptor (autoreleased)
    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::renderPassDescriptor();
    if (!passDesc) {
        LOG_ERROR("GHI_Metal", "Failed to create render pass descriptor");
        return;
    }
    
    // Configure color attachment
    MTL::RenderPassColorAttachmentDescriptorArray* colorAttachments = passDesc->colorAttachments();
    MTL::RenderPassColorAttachmentDescriptor* colorAttach = colorAttachments->object(0);
    
    if (colorAttach) {
        colorAttach->setTexture(drawable->texture());
        colorAttach->setLoadAction(MTL::LoadActionClear);
        colorAttach->setStoreAction(MTL::StoreActionStore);
        colorAttach->setClearColor(MTL::ClearColor::Make(
            currentState_.clearColor.r,
            currentState_.clearColor.g,
            currentState_.clearColor.b,
            currentState_.clearColor.a
        ));
    } else {
        LOG_ERROR("GHI_Metal", "Failed to get color attachment descriptor");
        return;
    }
    
    // Configure depth attachment (for proper 3D rendering)
    if (depthTexture_) {
        MTL::Texture* depthTex = static_cast<MTL::Texture*>(depthTexture_);
        MTL::RenderPassDepthAttachmentDescriptor* depthAttach = passDesc->depthAttachment();
        depthAttach->setTexture(depthTex);
        depthAttach->setLoadAction(MTL::LoadActionClear);
        depthAttach->setStoreAction(MTL::StoreActionDontCare);
        depthAttach->setClearDepth(1.0);  // Far plane = 1.0
    }
    
    // Create render command encoder
    MTL::RenderCommandEncoder* encoder = cmdBuffer->renderCommandEncoder(passDesc);
    
    if (!encoder) {
        LOG_ERROR("GHI_Metal", "Failed to create render encoder");
        return;
    }
    
    // Set depth stencil state and culling for proper 3D rendering
    if (depthStencilState_) {
        MTL::DepthStencilState* depthState = static_cast<MTL::DepthStencilState*>(depthStencilState_);
        encoder->setDepthStencilState(depthState);
    }
    encoder->setCullMode(MTL::CullModeBack);  // Cull back faces
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);  // CCW = front face
    
    currentRenderEncoder_ = encoder;
}

void GHI_MetalBackend::beginRenderPass(RenderTargetHandle target) {
    if (!currentCommandBuffer_) {
        LOG_WARN("GHI_Metal", "Cannot begin render pass: no command buffer");
        return;
    }
    
    if (!target.isValid()) {
        // Fall back to swapchain render pass
        beginRenderPass();
        return;
    }
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid render target handle: %u", target.id);
        return;
    }
    
    MetalRenderTargetData& rt = it->second;
    currentRenderTarget_ = target;
    
    MTL::CommandBuffer* cmdBuffer = static_cast<MTL::CommandBuffer*>(currentCommandBuffer_);
    
    // Create render pass descriptor
    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::renderPassDescriptor();
    if (!passDesc) {
        LOG_ERROR("GHI_Metal", "Failed to create render pass descriptor");
        return;
    }
    
    // Configure color attachments
    for (size_t i = 0; i < rt.colorTextures.size(); i++) {
        auto texIt = textures_.find(rt.colorTextures[i].id);
        if (texIt == textures_.end()) continue;
        
        MTL::Texture* colorTex = static_cast<MTL::Texture*>(texIt->second.texture);
        MTL::RenderPassColorAttachmentDescriptor* colorAttach = passDesc->colorAttachments()->object(i);
        colorAttach->setTexture(colorTex);
        colorAttach->setLoadAction(MTL::LoadActionClear);
        colorAttach->setStoreAction(MTL::StoreActionStore);
        if (i < rt.clearColors.size()) {
            colorAttach->setClearColor(MTL::ClearColor::Make(
                rt.clearColors[i].r, rt.clearColors[i].g,
                rt.clearColors[i].b, rt.clearColors[i].a
            ));
        }
    }
    
    // Configure depth attachment
    if (rt.depthTexture.isValid()) {
        auto depthIt = textures_.find(rt.depthTexture.id);
        if (depthIt != textures_.end()) {
            MTL::Texture* depthTex = static_cast<MTL::Texture*>(depthIt->second.texture);
            MTL::RenderPassDepthAttachmentDescriptor* depthAttach = passDesc->depthAttachment();
            depthAttach->setTexture(depthTex);
            depthAttach->setLoadAction(MTL::LoadActionClear);
            depthAttach->setStoreAction(MTL::StoreActionStore);
            depthAttach->setClearDepth(rt.depthClearValue);
        }
    }
    
    // Create render encoder
    MTL::RenderCommandEncoder* encoder = cmdBuffer->renderCommandEncoder(passDesc);
    if (!encoder) {
        LOG_ERROR("GHI_Metal", "Failed to create render encoder for target");
        return;
    }
    
    // Set depth state and culling
    if (depthStencilState_) {
        MTL::DepthStencilState* depthState = static_cast<MTL::DepthStencilState*>(depthStencilState_);
        encoder->setDepthStencilState(depthState);
    }
    encoder->setCullMode(MTL::CullModeBack);
    encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);
    
    currentRenderEncoder_ = encoder;
    
    LOG_INFO("GHI_Metal", "Begin render pass to target: id=%u, size=%ux%u",
             target.id, rt.width, rt.height);
}

void GHI_MetalBackend::endRenderPass() {
    if (currentRenderEncoder_) {
        MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
        encoder->endEncoding();
        currentRenderEncoder_ = nullptr;
    }
    
    currentRenderPassDescriptor_ = nullptr;
    currentRenderTarget_ = RenderTargetHandle{};
}

// ============================================================================
// Render Target Management
// ============================================================================

RenderTargetHandle GHI_MetalBackend::createRenderTarget(const RenderTargetCreateInfo& info) {
    if (!device_) {
        LOG_ERROR("GHI_Metal", "Cannot create render target: device not initialized");
        return RenderTargetHandle{};
    }
    
    if (info.width == 0 || info.height == 0) {
        LOG_ERROR("GHI_Metal", "Cannot create render target: invalid dimensions");
        return RenderTargetHandle{};
    }
    
    MetalRenderTargetData rt;
    rt.width = info.width;
    rt.height = info.height;
    
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    // Create color attachments
    for (const auto& colorAttach : info.colorAttachments) {
        TextureCreateInfo texInfo;
        texInfo.type = TextureType::Texture2D;
        texInfo.format = colorAttach.format;
        texInfo.width = info.width;
        texInfo.height = info.height;
        texInfo.mipLevels = 1;
        texInfo.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        
        TextureHandle colorTex = createTexture(texInfo);
        if (!colorTex.isValid()) {
            LOG_ERROR("GHI_Metal", "Failed to create color attachment");
            for (auto& tex : rt.colorTextures) {
                destroyTexture(tex);
            }
            return RenderTargetHandle{};
        }
        rt.colorTextures.push_back(colorTex);
        rt.clearColors.push_back(colorAttach.clearColor);
    }
    
    // Create depth attachment
    if (info.hasDepth) {
        MTL::TextureDescriptor* depthDesc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatDepth32Float,
            info.width, info.height,
            false
        );
        depthDesc->setStorageMode(MTL::StorageModePrivate);
        depthDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        
        MTL::Texture* depthTex = mtlDevice->newTexture(depthDesc);
        if (!depthTex) {
            LOG_ERROR("GHI_Metal", "Failed to create depth attachment");
            for (auto& tex : rt.colorTextures) {
                destroyTexture(tex);
            }
            return RenderTargetHandle{};
        }
        
        TextureHandle depthHandle;
        depthHandle.id = nextTextureID_++;
        
        MetalTextureData texData;
        texData.texture = depthTex;
        texData.sampler = nullptr; // Depth textures usually don't need sampler here
        textures_[depthHandle.id] = texData;
        
        rt.depthTexture = depthHandle;
        rt.depthClearValue = info.depthClearValue;
    }
    
    RenderTargetHandle handle;
    handle.id = nextRenderTargetID_++;
    renderTargets_[handle.id] = std::move(rt);
    
    LOG_INFO("GHI_Metal", "Created render target: id=%u, size=%ux%u, colors=%zu, depth=%s",
             handle.id, info.width, info.height, info.colorAttachments.size(),
             info.hasDepth ? "yes" : "no");
    
    return handle;
}

void GHI_MetalBackend::destroyRenderTarget(RenderTargetHandle handle) {
    if (!handle.isValid()) return;
    
    auto it = renderTargets_.find(handle.id);
    if (it == renderTargets_.end()) return;
    
    MetalRenderTargetData& rt = it->second;
    
    // Destroy textures
    for (auto& tex : rt.colorTextures) {
        destroyTexture(tex);
    }
    if (rt.depthTexture.isValid()) {
        destroyTexture(rt.depthTexture);
    }
    
    renderTargets_.erase(it);
    
    LOG_INFO("GHI_Metal", "Destroyed render target: id=%u", handle.id);
}

TextureHandle GHI_MetalBackend::getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) {
    if (!target.isValid()) return TextureHandle{};
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return TextureHandle{};
    
    if (index >= it->second.colorTextures.size()) {
        LOG_ERROR("GHI_Metal", "Color attachment index out of range: %u", index);
        return TextureHandle{};
    }
    
    return it->second.colorTextures[index];
}

TextureHandle GHI_MetalBackend::getRenderTargetDepthTexture(RenderTargetHandle target) {
    if (!target.isValid()) return TextureHandle{};
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return TextureHandle{};
    
    return it->second.depthTexture;
}

void GHI_MetalBackend::resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) {
    if (!target.isValid()) return;
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return;
    
    LOG_WARN("GHI_Metal", "Render target resize not yet implemented: id=%u, %ux%u -> %ux%u",
             target.id, it->second.width, it->second.height, width, height);
}

// ============================================================================
// Helpers
// ============================================================================

static MTL::BlendFactor convertBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return MTL::BlendFactorZero;
        case BlendFactor::One: return MTL::BlendFactorOne;
        case BlendFactor::SrcColor: return MTL::BlendFactorSourceColor;
        case BlendFactor::OneMinusSrcColor: return MTL::BlendFactorOneMinusSourceColor;
        case BlendFactor::DstColor: return MTL::BlendFactorDestinationColor;
        case BlendFactor::OneMinusDstColor: return MTL::BlendFactorOneMinusDestinationColor;
        case BlendFactor::SrcAlpha: return MTL::BlendFactorSourceAlpha;
        case BlendFactor::OneMinusSrcAlpha: return MTL::BlendFactorOneMinusSourceAlpha;
        case BlendFactor::DstAlpha: return MTL::BlendFactorDestinationAlpha;
        case BlendFactor::OneMinusDstAlpha: return MTL::BlendFactorOneMinusDestinationAlpha;
        default: return MTL::BlendFactorOne;
    }
}

static MTL::BlendOperation convertBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return MTL::BlendOperationAdd;
        case BlendOp::Subtract: return MTL::BlendOperationSubtract;
        case BlendOp::ReverseSubtract: return MTL::BlendOperationReverseSubtract;
        case BlendOp::Min: return MTL::BlendOperationMin;
        case BlendOp::Max: return MTL::BlendOperationMax;
        default: return MTL::BlendOperationAdd;
    }
}

static MTL::CompareFunction convertCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return MTL::CompareFunctionNever;
        case CompareOp::Less: return MTL::CompareFunctionLess;
        case CompareOp::Equal: return MTL::CompareFunctionEqual;
        case CompareOp::LessOrEqual: return MTL::CompareFunctionLessEqual;
        case CompareOp::Greater: return MTL::CompareFunctionGreater;
        case CompareOp::NotEqual: return MTL::CompareFunctionNotEqual;
        case CompareOp::GreaterOrEqual: return MTL::CompareFunctionGreaterEqual;
        case CompareOp::Always: return MTL::CompareFunctionAlways;
        default: return MTL::CompareFunctionLess;
    }
}

// Helper to check file extension
static bool endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

ShaderHandle GHI_MetalBackend::createShader(const ShaderSource& source) {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    // Load .metal file or compile source
    NS::Error* error = nullptr;
    MTL::Library* library = nullptr;
    
    if (source.vertexPath || source.fragmentPath) {
        // For now, assume both shaders in same file or paired .vert.spv/.frag.spv
        const char* shaderPath = source.vertexPath ? source.vertexPath : source.fragmentPath;
        std::string pathStr(shaderPath);
        
        LOG_INFO("GHI_Metal", "Loading shader from: %s", shaderPath);
        
        std::string mslSource;
        
        // Check if this is a SPIR-V file - use SPIRV-Cross to convert
        if (endsWith(pathStr, ".spv")) {
            LOG_INFO("GHI_Metal", "Detected SPIR-V file, converting to MSL via SPIRV-Cross");
            
            // For paired vertex/fragment shaders, we need to combine them
            // SPIRV-Cross generates MSL with vertexMain and fragmentMain functions
            std::string vertMSL, fragMSL;
            
            if (source.vertexPath) {
                vertMSL = spirvToMSL(source.vertexPath);
                if (vertMSL.empty()) {
                    LOG_ERROR("GHI_Metal", "Failed to cross-compile vertex shader: %s", source.vertexPath);
                    pPool->release();
                    return ShaderHandle{};
                }
                LOG_INFO("GHI_Metal", "Converted vertex shader to MSL (%zu bytes)", vertMSL.size());
            }
            
            if (source.fragmentPath) {
                fragMSL = spirvToMSL(source.fragmentPath);
                if (fragMSL.empty()) {
                    LOG_ERROR("GHI_Metal", "Failed to cross-compile fragment shader: %s", source.fragmentPath);
                    pPool->release();
                    return ShaderHandle{};
                }
                LOG_INFO("GHI_Metal", "Converted fragment shader to MSL (%zu bytes)", fragMSL.size());
            }
            
            // Combine vertex and fragment MSL (they're in separate SPIR-V modules)
            // SPIRV-Cross names the entry points 'main' - we need to rename them
            // For now, we'll compile them separately and create a combined pipeline
            // Actually, SPIRV-Cross should generate separate entry points
            mslSource = vertMSL + "\n" + fragMSL;
        } else {
            // .metal file - read directly
            FILE* file = fopen(shaderPath, "r");
            if (!file) {
                LOG_ERROR("GHI_Metal", "Failed to open shader file: %s (errno: %d)", shaderPath, errno);
                pPool->release();
                return ShaderHandle{};
            }
            
            LOG_INFO("GHI_Metal", "Shader file opened successfully");
            
            fseek(file, 0, SEEK_END);
            long fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);
            
            char* sourceCode = (char*)malloc(fileSize + 1);
            fread(sourceCode, 1, fileSize, file);
            sourceCode[fileSize] = '\0';
            fclose(file);
            
            mslSource = std::string(sourceCode);
            free(sourceCode);
        }
        
        // Compile MSL source
        NS::String* src = NS::String::string(mslSource.c_str(), NS::UTF8StringEncoding);
        
        MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
        library = mtlDevice->newLibrary(src, options, &error);
        options->release();
        
        if (error) {
            LOG_ERROR("GHI_Metal", "Failed to compile shader: %s", error->localizedDescription()->utf8String());
            pPool->release();
            return ShaderHandle{};
        }
    } else if (source.vertexSource) {
        // Compile from source string
        NS::String* src = NS::String::string(source.vertexSource, NS::UTF8StringEncoding);
        MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
        library = mtlDevice->newLibrary(src, options, &error);
        options->release();
        
        if (error) {
            LOG_ERROR("GHI_Metal", "Failed to compile shader: %s", error->localizedDescription()->utf8String());
            pPool->release();
            return ShaderHandle{};
        }
    } else {
        LOG_ERROR("GHI_Metal", "No shader source or path provided");
        pPool->release();
        return ShaderHandle{};
    }
    
    if (!library) {
        LOG_ERROR("GHI_Metal", "Failed to create shader library");
        pPool->release();
        return ShaderHandle{};
    }
    
    // Get vertex and fragment functions
    NS::String* vertName = NS::String::string("vertexMain", NS::UTF8StringEncoding);
    NS::String* fragName = NS::String::string("fragmentMain", NS::UTF8StringEncoding);
    
    MTL::Function* vertFunc = library->newFunction(vertName);
    MTL::Function* fragFunc = library->newFunction(fragName);
    
    if (!vertFunc || !fragFunc) {
        LOG_ERROR("GHI_Metal", "Failed to find shader functions (vertexMain/fragmentMain)");
        if (vertFunc) vertFunc->release();
        if (fragFunc) fragFunc->release();
        library->release();
        pPool->release();
        return ShaderHandle{};
    }
    
    // Create render pipeline descriptor
    MTL::RenderPipelineDescriptor* pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipelineDesc->setVertexFunction(vertFunc);
    pipelineDesc->setFragmentFunction(fragFunc);
    
    // Set pixel format (match swapchain - sRGB for proper gamma)
    pipelineDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
    
    // Create vertex descriptor for uber shader
    // Layout: position (float3), normal (float3), texCoord (float2)
    // IMPORTANT: Use buffer index 30 for vertex data to avoid conflict with uniform buffers (0, 1, 2...)
    // Metal shares buffer indices between vertex data and uniform data
    static const uint32_t VERTEX_BUFFER_INDEX = 30;
    
    MTL::VertexDescriptor* vertexDesc = MTL::VertexDescriptor::alloc()->init();
    
    // Attribute 0: position (float3)
    vertexDesc->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
    vertexDesc->attributes()->object(0)->setOffset(0);
    vertexDesc->attributes()->object(0)->setBufferIndex(VERTEX_BUFFER_INDEX);
    
    // Attribute 1: normal (float3)
    vertexDesc->attributes()->object(1)->setFormat(MTL::VertexFormatFloat3);
    vertexDesc->attributes()->object(1)->setOffset(3 * sizeof(float));  // After position
    vertexDesc->attributes()->object(1)->setBufferIndex(VERTEX_BUFFER_INDEX);
    
    // Attribute 2: texCoord (float2)
    vertexDesc->attributes()->object(2)->setFormat(MTL::VertexFormatFloat2);
    vertexDesc->attributes()->object(2)->setOffset(6 * sizeof(float));  // After position + normal
    vertexDesc->attributes()->object(2)->setBufferIndex(VERTEX_BUFFER_INDEX);
    
    // Layout 30: stride = sizeof(float3 + float3 + float2) = 8 floats = 32 bytes
    vertexDesc->layouts()->object(VERTEX_BUFFER_INDEX)->setStride(8 * sizeof(float));
    vertexDesc->layouts()->object(VERTEX_BUFFER_INDEX)->setStepFunction(MTL::VertexStepFunctionPerVertex);
    
    pipelineDesc->setVertexDescriptor(vertexDesc);
    vertexDesc->release();
    
    // Set depth attachment format for proper 3D rendering
    pipelineDesc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    
    // Store shader data for later pipeline creation
    MetalShaderData shaderData;
    shaderData.vertexFunction = vertFunc;    // Already has +1 ref from newFunction
    shaderData.fragmentFunction = fragFunc;  // Already has +1 ref from newFunction
    shaderData.vertexDescriptor = vertexDesc; // Already has +1 ref from alloc
    
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    shaders_[handle.id] = shaderData;
    
    // Cleanup descriptor (not the functions or vertex descriptor)
    pipelineDesc->release();
    library->release();
    
    LOG_INFO("GHI_Metal", "Created shader (deferred pipeline creation): id=%u", handle.id);
    
    pPool->release();
    return handle;
}

void GHI_MetalBackend::draw(uint32_t vertexCount, uint32_t instanceCount, 
                           uint32_t firstVertex, uint32_t firstInstance) {
    if (!currentRenderEncoder_) {
        LOG_ERROR("GHI_Metal", "Cannot draw: no active render encoder");
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    
    // Draw primitives (metal-cpp C++ API)
    encoder->drawPrimitives(
        MTL::PrimitiveTypeTriangle,
        NS::UInteger(firstVertex),
        NS::UInteger(vertexCount),
        NS::UInteger(instanceCount),
        NS::UInteger(firstInstance)
    );
    
    LOG_INFO("GHI_Metal", "Draw: %u vertices", vertexCount);
}

void GHI_MetalBackend::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!currentRenderEncoder_) {
        LOG_ERROR("GHI_Metal", "Cannot draw indexed: no active render encoder");
        return;
    }
    
    if (!boundIndexBuffer_.isValid()) {
        LOG_ERROR("GHI_Metal", "Cannot draw indexed: no index buffer bound");
        return;
    }
    
    auto it = buffers_.find(boundIndexBuffer_.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid index buffer handle: %u", boundIndexBuffer_.id);
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(it->second);
    
    // Draw indexed primitives (metal-cpp C++ API)
    encoder->drawIndexedPrimitives(
        MTL::PrimitiveTypeTriangle,
        NS::UInteger(indexCount),
        MTL::IndexTypeUInt16,
        indexBuffer,
        NS::UInteger(boundIndexBufferOffset_ + firstIndex * sizeof(uint16_t)),
        NS::UInteger(instanceCount),
        NS::Integer(vertexOffset),
        NS::UInteger(firstInstance)
    );
    
    // Per-frame logging disabled: LOG_INFO("GHI_Metal", "drawIndexed: %u indices, %u instances", indexCount, instanceCount);
}

void GHI_MetalBackend::bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) {
    if (!currentRenderEncoder_) {
        LOG_ERROR("GHI_Metal", "Cannot bind vertex buffer: no encoder");
        return;
    }

    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid buffer handle: %u", buffer.id);
        return;
    }

    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);

    // Metal shares buffer indices between vertex data and uniforms.
    // Always use buffer index 30 for vertex data to avoid conflict with uniform buffers (0, 1, 2...)
    // This matches the vertex descriptor configuration in createShader()
    static const uint32_t VERTEX_BUFFER_INDEX = 30;
    
    // Bind vertex buffer (metal-cpp C++ API)
    encoder->setVertexBuffer(mtlBuffer, NS::UInteger(offset), NS::UInteger(VERTEX_BUFFER_INDEX));
    
    // Per-frame logging disabled: LOG_INFO("GHI_Metal", "Vertex buffer bound: id=%u", buffer.id);
}

void GHI_MetalBackend::bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    if (!currentRenderEncoder_) {
        LOG_ERROR("GHI_Metal", "Cannot bind uniform: no encoder");
        return;
    }

    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid buffer handle: %u", buffer.id);
        return;
    }

    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
    
    // Use setVertexBytes for small uniform data (like SDL Metal example)
    // This copies data directly instead of binding buffer resource
    void* data = mtlBuffer->contents();
    NS::UInteger size = mtlBuffer->length();

    // IMPORTANT: Metal uses a single flat buffer index space, while SPIR-V has (set,binding).
    // We rely on SPIRV-Cross remapping (see ghi_shader_cross.cpp) to map (set,binding) -> msl_buffer.
    // Keep this mapping in sync with shader-cross:
    //   - buffer(0): set 0 binding 0
    //   - buffer(1): push constants
    //   - buffer(2 + set*8 + binding): all other uniform buffers
    uint32_t mslBufferIndex = 0;
    if (set == 0 && binding == 0) {
        mslBufferIndex = 0;
    } else {
        mslBufferIndex = 2 + set * 8 + binding;
    }
    
    encoder->setVertexBytes(data, size, mslBufferIndex);
    encoder->setFragmentBytes(data, size, mslBufferIndex);
    
    // Per-frame logging disabled: LOG_INFO("GHI_Metal", "Uniform BYTES bound");
}

void GHI_MetalBackend::bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    if (!currentRenderEncoder_ || !texture.isValid()) {
        return;
    }
    
    auto it = textures_.find(texture.id);
    if (it == textures_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid texture handle: %u", texture.id);
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    
    // Map set/binding to texture index
    NS::UInteger textureIndex = set * 10 + binding;
    
    // Bind texture and its associated sampler
    if (it->second.texture) {
        encoder->setFragmentTexture(static_cast<MTL::Texture*>(it->second.texture), textureIndex);
    }
    
    if (it->second.sampler) {
        encoder->setFragmentSamplerState(static_cast<MTL::SamplerState*>(it->second.sampler), textureIndex);
    }
}

void GHI_MetalBackend::bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid storage buffer handle: %u", buffer.id);
        return;
    }
    
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
    NS::UInteger bufferIndex = set * 10 + binding;
    
    // Bind to compute encoder if active
    if (currentComputeEncoder_) {
        MTL::ComputeCommandEncoder* encoder = static_cast<MTL::ComputeCommandEncoder*>(currentComputeEncoder_);
        encoder->setBuffer(mtlBuffer, 0, bufferIndex);
    }
    // Also bind to render encoder if active (for vertex/fragment access)
    else if (currentRenderEncoder_) {
        MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
        encoder->setVertexBuffer(mtlBuffer, 0, bufferIndex);
        encoder->setFragmentBuffer(mtlBuffer, 0, bufferIndex);
    }
}

void GHI_MetalBackend::bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    auto it = textures_.find(texture.id);
    if (it == textures_.end()) {
        LOG_ERROR("GHI_Metal", "Invalid storage texture handle: %u", texture.id);
        return;
    }
    
    MTL::Texture* mtlTexture = static_cast<MTL::Texture*>(it->second.texture);
    NS::UInteger textureIndex = set * 10 + binding;
    
    // Bind to compute encoder for storage image access
    if (currentComputeEncoder_) {
        MTL::ComputeCommandEncoder* encoder = static_cast<MTL::ComputeCommandEncoder*>(currentComputeEncoder_);
        encoder->setTexture(mtlTexture, textureIndex);
    } else {
        LOG_WARN("GHI_Metal", "Cannot bind storage texture: no compute encoder active");
    }
}

void GHI_MetalBackend::setPushConstants(const void* data, uint32_t size, uint32_t offset) {
    // Metal uses setVertexBytes for inline constant data (equivalent to push constants)
    // Push constants are remapped to buffer(1) in SPIRV-Cross (see ghi_shader_cross.cpp)
    if (currentRenderEncoder_) {
        MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
        encoder->setVertexBytes(data, size, 1);  // buffer(1) matches SPIRV-Cross remapping
    }
}

void GHI_MetalBackend::setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!currentRenderEncoder_) {
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    
    // Set viewport (metal-cpp C++ API)
    MTL::Viewport viewport = {
        static_cast<double>(x),
        static_cast<double>(y),
        static_cast<double>(width),
        static_cast<double>(height),
        0.0,  // znear
        1.0   // zfar
    };
    encoder->setViewport(viewport);
    
    // Per-frame logging disabled: LOG_INFO("GHI_Metal", "Viewport set");
}

void GHI_MetalBackend::setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!currentRenderEncoder_) {
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    
    // Set scissor rect (metal-cpp C++ API)
    MTL::ScissorRect scissor = {
        NS::UInteger(x),
        NS::UInteger(y),
        NS::UInteger(width),
        NS::UInteger(height)
    };
    encoder->setScissorRect(scissor);
}

ShaderHandle GHI_MetalBackend::createComputeShader(const ShaderSource& source) {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    MTL::Device* device = static_cast<MTL::Device*>(device_);
    
    // Load compute shader source
    std::string computeSourceStr;
    if (source.computePath) {
        std::string path(source.computePath);
        
        // If path ends with .spv, convert SPIR-V to MSL
        if (path.length() > 4 && path.substr(path.length() - 4) == ".spv") {
            LOG_INFO("GHI_Metal", "Loading compute shader from: %s", source.computePath);
            LOG_INFO("GHI_Metal", "Detected SPIR-V file, converting to MSL via SPIRV-Cross");
            
            computeSourceStr = spirvToMSL(source.computePath);
            if (computeSourceStr.empty()) {
                LOG_ERROR("GHI_Metal", "Failed to convert SPIR-V compute shader to MSL");
                pPool->release();
                return ShaderHandle{};
            }
        } else {
            // Load MSL source directly
            std::ifstream file(source.computePath);
            if (!file) {
                LOG_ERROR("GHI_Metal", "Failed to open compute shader: %s", source.computePath);
                pPool->release();
                return ShaderHandle{};
            }
            computeSourceStr = std::string((std::istreambuf_iterator<char>(file)),
                                            std::istreambuf_iterator<char>());
        }
    } else if (source.computeSource) {
        computeSourceStr = source.computeSource;
    } else {
        LOG_ERROR("GHI_Metal", "No compute shader source provided");
        pPool->release();
        return ShaderHandle{};
    }
    
    // Create MTL library from MSL source
    NS::Error* error = nullptr;
    NS::String* sourceNS = NS::String::string(computeSourceStr.c_str(), NS::UTF8StringEncoding);
    MTL::Library* library = device->newLibrary(sourceNS, nullptr, &error);
    
    if (!library) {
        LOG_ERROR("GHI_Metal", "Failed to compile compute shader: %s",
                  error ? error->localizedDescription()->utf8String() : "unknown error");
        pPool->release();
        return ShaderHandle{};
    }
    
    // Get compute function (try "computeMain" or "main")
    MTL::Function* computeFunc = library->newFunction(NS::String::string("computeMain", NS::UTF8StringEncoding));
    if (!computeFunc) {
        computeFunc = library->newFunction(NS::String::string("main0", NS::UTF8StringEncoding));
    }
    if (!computeFunc) {
        computeFunc = library->newFunction(NS::String::string("main", NS::UTF8StringEncoding));
    }
    
    if (!computeFunc) {
        LOG_ERROR("GHI_Metal", "Failed to find compute function (tried computeMain, main0, main)");
        library->release();
        pPool->release();
        return ShaderHandle{};
    }
    
    // Create compute pipeline state
    MTL::ComputePipelineState* computePipeline = device->newComputePipelineState(computeFunc, &error);
    
    if (!computePipeline) {
        LOG_ERROR("GHI_Metal", "Failed to create compute pipeline: %s",
                  error ? error->localizedDescription()->utf8String() : "unknown error");
        computeFunc->release();
        library->release();
        pPool->release();
        return ShaderHandle{};
    }
    
    // Store and return handle
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    computePipelines_[handle.id] = computePipeline;
    
    computeFunc->release();
    library->release();
    
    LOG_INFO("GHI_Metal", "Created compute shader pipeline: id=%u", handle.id);
    pPool->release();
    return handle;
}

void GHI_MetalBackend::bindComputeShader(ShaderHandle shader) {
    if (!shader.isValid()) {
        LOG_ERROR("GHI_Metal", "Cannot bind invalid compute shader");
        return;
    }
    
    auto it = computePipelines_.find(shader.id);
    if (it == computePipelines_.end()) {
        LOG_ERROR("GHI_Metal", "Compute shader not found: id=%u", shader.id);
        return;
    }
    
    // Create compute encoder if not already active
    if (!currentComputeEncoder_) {
        MTL::CommandBuffer* cmdBuffer = static_cast<MTL::CommandBuffer*>(currentCommandBuffer_);
        if (!cmdBuffer) {
            LOG_ERROR("GHI_Metal", "Cannot bind compute shader: no active command buffer");
            return;
        }
        
        currentComputeEncoder_ = cmdBuffer->computeCommandEncoder();
        if (!currentComputeEncoder_) {
            LOG_ERROR("GHI_Metal", "Failed to create compute command encoder");
            return;
        }
    }
    
    MTL::ComputeCommandEncoder* encoder = static_cast<MTL::ComputeCommandEncoder*>(currentComputeEncoder_);
    MTL::ComputePipelineState* pipeline = static_cast<MTL::ComputePipelineState*>(it->second);
    
    encoder->setComputePipelineState(pipeline);
    
    LOG_INFO("GHI_Metal", "Bound compute shader: id=%u", shader.id);
}

void GHI_MetalBackend::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    if (!currentComputeEncoder_) {
        LOG_ERROR("GHI_Metal", "Cannot dispatch: no active compute encoder");
        pPool->release();
        return;
    }
    
    MTL::ComputeCommandEncoder* encoder = static_cast<MTL::ComputeCommandEncoder*>(currentComputeEncoder_);
    
    // Dispatch compute (metal-cpp C++ API)
    MTL::Size gridSize = MTL::Size::Make(groupCountX, groupCountY, groupCountZ);
    MTL::Size threadgroupSize = MTL::Size::Make(8, 8, 1);  // TODO: Get from shader
    
    encoder->dispatchThreadgroups(gridSize, threadgroupSize);
    
    pPool->release();
}

// Stub implementations for remaining methods

void GHI_MetalBackend::drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!currentRenderEncoder_) {
        LOG_ERROR("GHI_Metal", "drawIndirect: No active render encoder");
        return;
    }
    
    if (!indirectBuffer.isValid()) {
        LOG_ERROR("GHI_Metal", "drawIndirect: Invalid indirect buffer handle");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "drawIndirect: Buffer not found: id=%u", indirectBuffer.id);
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
    
    // Draw multiple indirect commands
    for (uint32_t i = 0; i < drawCount; i++) {
        NS::UInteger offset = i * stride;
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, mtlBuffer, offset);
    }
    
    LOG_INFO("GHI_Metal", "drawIndirect: %u draws, stride=%u", drawCount, stride);
}

void GHI_MetalBackend::drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!currentRenderEncoder_) {
        LOG_ERROR("GHI_Metal", "drawIndexedIndirect: No active render encoder");
        return;
    }
    
    if (!indirectBuffer.isValid() || !boundIndexBuffer_.isValid()) {
        LOG_ERROR("GHI_Metal", "drawIndexedIndirect: Invalid buffer handles");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    auto indexIt = buffers_.find(boundIndexBuffer_.id);
    if (it == buffers_.end() || indexIt == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "drawIndexedIndirect: Buffer not found");
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
    MTL::Buffer* indexBuffer = static_cast<MTL::Buffer*>(indexIt->second);
    
    // Draw multiple indexed indirect commands
    for (uint32_t i = 0; i < drawCount; i++) {
        NS::UInteger offset = i * stride;
        encoder->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle,
            MTL::IndexTypeUInt16,
            indexBuffer,
            boundIndexBufferOffset_,
            mtlBuffer,
            offset
        );
    }
    
    LOG_INFO("GHI_Metal", "drawIndexedIndirect: %u draws, stride=%u", drawCount, stride);
}

void GHI_MetalBackend::dispatchIndirect(BufferHandle indirectBuffer) {
    if (!currentComputeEncoder_) {
        LOG_ERROR("GHI_Metal", "dispatchIndirect: No active compute encoder");
        return;
    }
    
    if (!indirectBuffer.isValid()) {
        LOG_ERROR("GHI_Metal", "dispatchIndirect: Invalid indirect buffer handle");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) {
        LOG_ERROR("GHI_Metal", "dispatchIndirect: Buffer not found: id=%u", indirectBuffer.id);
        return;
    }
    
    MTL::ComputeCommandEncoder* encoder = static_cast<MTL::ComputeCommandEncoder*>(currentComputeEncoder_);
    MTL::Buffer* mtlBuffer = static_cast<MTL::Buffer*>(it->second);
    
    // Dispatch with indirect buffer
    // Note: Threadgroup size should come from the compute pipeline
    MTL::Size threadgroupSize = MTL::Size::Make(8, 8, 1);
    encoder->dispatchThreadgroups(mtlBuffer, 0, threadgroupSize);
    
    LOG_INFO("GHI_Metal", "dispatchIndirect: buffer=%u", indirectBuffer.id);
}

void GHI_MetalBackend::bindIndexBuffer(BufferHandle buffer, size_t offset) {
    // Metal binds index buffer in draw call, store for later
    boundIndexBuffer_ = buffer;
    boundIndexBufferOffset_ = offset;
}

void GHI_MetalBackend::memoryBarrier() {
    // Metal handles synchronization automatically via resource tracking
}

void GHI_MetalBackend::bufferBarrier(BufferHandle buffer) {
    // Metal handles synchronization automatically
}

void GHI_MetalBackend::textureBarrier(TextureHandle texture) {
    // Metal handles synchronization automatically
}

// ============================================================================
// Stubs for methods not yet implemented
// ============================================================================

TextureHandle GHI_MetalBackend::createTexture(const TextureCreateInfo& info) {
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    // Create texture descriptor
    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    
    // Set dimensions
    descriptor->setWidth(info.width);
    descriptor->setHeight(info.height);
    descriptor->setDepth(info.depth);
    descriptor->setMipmapLevelCount(info.mipLevels);
    
    // Set type
    switch (info.type) {
        case TextureType::Texture2D:
            descriptor->setTextureType(MTL::TextureType2D);
            break;
        case TextureType::TextureCube:
            descriptor->setTextureType(MTL::TextureTypeCube);
            break;
        case TextureType::Texture3D:
            descriptor->setTextureType(MTL::TextureType3D);
            break;
        case TextureType::TextureArray:
            descriptor->setTextureType(MTL::TextureType2DArray);
            break;
    }
    
    // Set format
    switch (info.format) {
        case Format::RGBA8_UNORM:
            descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
            break;
        case Format::RGBA8_SRGB:
            descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB);
            break;
        case Format::Depth32F:
            descriptor->setPixelFormat(MTL::PixelFormatDepth32Float);
            break;
        default:
            descriptor->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
            break;
    }

    if (hasUsage(info.usage, TextureUsage::RenderTarget) || hasUsage(info.usage, TextureUsage::DepthStencil)) {
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        descriptor->setStorageMode(MTL::StorageModePrivate);
    }
    
    // Create texture
    MTL::Texture* mtlTexture = mtlDevice->newTexture(descriptor);
    descriptor->release();
    
    if (!mtlTexture) {
        LOG_ERROR("GHI_Metal", "Failed to create texture");
        return TextureHandle{};
    }
    
    // Upload initial data if provided
    if (info.data) {
        MTL::Region region = MTL::Region::Make2D(0, 0, info.width, info.height);
        uint32_t bytesPerRow = info.width * 4;  // Assuming RGBA8
        mtlTexture->replaceRegion(region, 0, info.data, bytesPerRow);
    }

    // Create associated sampler
    MTL::SamplerDescriptor* samplerDesc = MTL::SamplerDescriptor::alloc()->init();
    
    auto toMtlFilter = [](Filter filter) -> MTL::SamplerMinMagFilter {
        switch (filter) {
            case Filter::Nearest:
            case Filter::Nearest_Mipmap_Nearest:
            case Filter::Nearest_Mipmap_Linear:
                return MTL::SamplerMinMagFilterNearest;
            default:
                return MTL::SamplerMinMagFilterLinear;
        }
    };
    
    samplerDesc->setMinFilter(toMtlFilter(info.minFilter));
    samplerDesc->setMagFilter(toMtlFilter(info.magFilter));
    
    auto toMtlAddressMode = [](WrapMode mode) -> MTL::SamplerAddressMode {
        switch (mode) {
            case WrapMode::Repeat: return MTL::SamplerAddressModeRepeat;
            case WrapMode::ClampToEdge: return MTL::SamplerAddressModeClampToEdge;
            case WrapMode::ClampToBorder: return MTL::SamplerAddressModeClampToBorderColor;
            case WrapMode::MirroredRepeat: return MTL::SamplerAddressModeMirrorRepeat;
            default: return MTL::SamplerAddressModeRepeat;
        }
    };
    
    samplerDesc->setSAddressMode(toMtlAddressMode(info.wrapS));
    samplerDesc->setTAddressMode(toMtlAddressMode(info.wrapT));
    samplerDesc->setRAddressMode(toMtlAddressMode(info.wrapR));

    MTL::SamplerState* mtlSampler = mtlDevice->newSamplerState(samplerDesc);
    samplerDesc->release();
    
    TextureHandle handle;
    handle.id = nextTextureID_++;
    
    MetalTextureData texData;
    texData.texture = mtlTexture;
    texData.sampler = mtlSampler;
    textures_[handle.id] = texData;
    
    LOG_INFO("GHI_Metal", "Created texture + sampler: id=%u, size=%ux%u", handle.id, info.width, info.height);
    
    return handle;
}

void GHI_MetalBackend::destroyTexture(TextureHandle handle) {
    auto it = textures_.find(handle.id);
    if (it != textures_.end()) {
        if (it->second.texture) static_cast<MTL::Texture*>(it->second.texture)->release();
        if (it->second.sampler) static_cast<MTL::SamplerState*>(it->second.sampler)->release();
        textures_.erase(it);
    }
}

void GHI_MetalBackend::updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* data) {
    // Stub
}

void GHI_MetalBackend::destroyShader(ShaderHandle handle) {
    if (!handle.isValid()) return;

    // 1. Destroy cached pipelines for this shader
    for (auto it = pipelineCache_.begin(); it != pipelineCache_.end(); ) {
        if (it->first.shaderId == handle.id) {
            if (it->second) static_cast<MTL::RenderPipelineState*>(it->second)->release();
            it = pipelineCache_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Destroy shader functions and descriptor
    auto it = shaders_.find(handle.id);
    if (it != shaders_.end()) {
        MetalShaderData& sd = it->second;
        if (sd.vertexFunction) static_cast<MTL::Function*>(sd.vertexFunction)->release();
        if (sd.fragmentFunction) static_cast<MTL::Function*>(sd.fragmentFunction)->release();
        if (sd.vertexDescriptor) static_cast<MTL::VertexDescriptor*>(sd.vertexDescriptor)->release();
        shaders_.erase(it);
    }
}

// ============================================================================
// Sampler Management
// ============================================================================

SamplerHandle GHI_MetalBackend::createSampler(const SamplerCreateInfo& info) {
    if (!device_) {
        LOG_ERROR("GHI_Metal", "Cannot create sampler: device not initialized");
        return SamplerHandle{};
    }
    
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    MTL::SamplerDescriptor* desc = MTL::SamplerDescriptor::alloc()->init();
    
    // Convert filter modes
    auto toMtlFilter = [](Filter filter) -> MTL::SamplerMinMagFilter {
        switch (filter) {
            case Filter::Nearest:
            case Filter::Nearest_Mipmap_Nearest:
            case Filter::Nearest_Mipmap_Linear:
                return MTL::SamplerMinMagFilterNearest;
            default:
                return MTL::SamplerMinMagFilterLinear;
        }
    };
    
    desc->setMinFilter(toMtlFilter(info.minFilter));
    desc->setMagFilter(toMtlFilter(info.magFilter));
    
    // Mipmap filter
    switch (info.mipFilter) {
        case Filter::Nearest_Mipmap_Nearest:
        case Filter::Linear_Mipmap_Nearest:
            desc->setMipFilter(MTL::SamplerMipFilterNearest);
            break;
        case Filter::Nearest_Mipmap_Linear:
        case Filter::Linear_Mipmap_Linear:
            desc->setMipFilter(MTL::SamplerMipFilterLinear);
            break;
        default:
            desc->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
            break;
    }
    
    // Address modes
    auto toMtlAddressMode = [](WrapMode mode) -> MTL::SamplerAddressMode {
        switch (mode) {
            case WrapMode::Repeat: return MTL::SamplerAddressModeRepeat;
            case WrapMode::ClampToEdge: return MTL::SamplerAddressModeClampToEdge;
            case WrapMode::ClampToBorder: return MTL::SamplerAddressModeClampToBorderColor;
            case WrapMode::MirroredRepeat: return MTL::SamplerAddressModeMirrorRepeat;
            default: return MTL::SamplerAddressModeRepeat;
        }
    };
    
    desc->setSAddressMode(toMtlAddressMode(info.wrapS));
    desc->setTAddressMode(toMtlAddressMode(info.wrapT));
    desc->setRAddressMode(toMtlAddressMode(info.wrapR));
    
    // Anisotropy
    if (info.anisotropyEnabled) {
        desc->setMaxAnisotropy(static_cast<NS::UInteger>(info.maxAnisotropy));
    }
    
    // LOD settings
    desc->setLodMinClamp(info.minLod);
    desc->setLodMaxClamp(info.maxLod);
    
    // Compare function (for shadow maps)
    if (info.compareEnabled) {
        auto toMtlCompareFunc = [](CompareOp op) -> MTL::CompareFunction {
            switch (op) {
                case CompareOp::Never: return MTL::CompareFunctionNever;
                case CompareOp::Less: return MTL::CompareFunctionLess;
                case CompareOp::Equal: return MTL::CompareFunctionEqual;
                case CompareOp::LessOrEqual: return MTL::CompareFunctionLessEqual;
                case CompareOp::Greater: return MTL::CompareFunctionGreater;
                case CompareOp::NotEqual: return MTL::CompareFunctionNotEqual;
                case CompareOp::GreaterOrEqual: return MTL::CompareFunctionGreaterEqual;
                case CompareOp::Always: return MTL::CompareFunctionAlways;
                default: return MTL::CompareFunctionLess;
            }
        };
        desc->setCompareFunction(toMtlCompareFunc(info.compareOp));
    }
    
    MTL::SamplerState* sampler = mtlDevice->newSamplerState(desc);
    desc->release();
    
    if (!sampler) {
        LOG_ERROR("GHI_Metal", "Failed to create sampler state");
        return SamplerHandle{};
    }
    
    SamplerHandle handle;
    handle.id = nextSamplerID_++;
    standaloneSamplers_[handle.id] = sampler;
    
    LOG_INFO("GHI_Metal", "Created sampler: id=%u, aniso=%s",
             handle.id, info.anisotropyEnabled ? "yes" : "no");
    
    return handle;
}

void GHI_MetalBackend::destroySampler(SamplerHandle handle) {
    if (!handle.isValid()) return;
    
    auto it = standaloneSamplers_.find(handle.id);
    if (it != standaloneSamplers_.end()) {
        MTL::SamplerState* sampler = static_cast<MTL::SamplerState*>(it->second);
        if (sampler) {
            sampler->release();
        }
        standaloneSamplers_.erase(it);
        LOG_INFO("GHI_Metal", "Destroyed sampler: id=%u", handle.id);
    }
}

void GHI_MetalBackend::bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) {
    if (!currentRenderEncoder_) {
        LOG_WARN("GHI_Metal", "bindSampler: No active render encoder");
        return;
    }
    
    if (!sampler.isValid()) {
        LOG_WARN("GHI_Metal", "bindSampler: Invalid sampler handle");
        return;
    }
    
    auto it = standaloneSamplers_.find(sampler.id);
    if (it == standaloneSamplers_.end()) {
        LOG_ERROR("GHI_Metal", "bindSampler: Sampler not found: id=%u", sampler.id);
        return;
    }
    
    MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);
    MTL::SamplerState* mtlSampler = static_cast<MTL::SamplerState*>(it->second);
    
    // Bind to both vertex and fragment stages
    encoder->setVertexSamplerState(mtlSampler, binding);
    encoder->setFragmentSamplerState(mtlSampler, binding);
    
    LOG_INFO("GHI_Metal", "Bound sampler: id=%u to binding=%u", sampler.id, binding);
}

void GHI_MetalBackend::setRenderState(const RenderState& state) {
    currentState_ = state;
    
    // Apply pipeline state if we have an active encoder
    if (currentRenderEncoder_ && state.shader.isValid()) {
        MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
        MTL::RenderCommandEncoder* encoder = static_cast<MTL::RenderCommandEncoder*>(currentRenderEncoder_);

        // 1. Handle Pipeline State (Shader + Blending)
        PipelineKey pipeKey;
        pipeKey.shaderId = state.shader.id;
        pipeKey.blendEnabled = state.blendEnabled;
        pipeKey.srcColorBlendFactor = state.srcColorBlendFactor;
        pipeKey.dstColorBlendFactor = state.dstColorBlendFactor;
        pipeKey.colorBlendOp = state.colorBlendOp;
        pipeKey.srcAlphaBlendFactor = state.srcAlphaBlendFactor;
        pipeKey.dstAlphaBlendFactor = state.dstAlphaBlendFactor;
        pipeKey.alphaBlendOp = state.alphaBlendOp;

        auto pipeIt = pipelineCache_.find(pipeKey);
        MTL::RenderPipelineState* pipeline = nullptr;

        if (pipeIt != pipelineCache_.end()) {
            pipeline = static_cast<MTL::RenderPipelineState*>(pipeIt->second);
        } else {
            // Create new pipeline variant
            auto shaderIt = shaders_.find(state.shader.id);
            if (shaderIt == shaders_.end()) {
                LOG_ERROR("GHI_Metal", "Shader id=%u not found", state.shader.id);
                return;
            }

            MetalShaderData& sd = shaderIt->second;
            MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
            desc->setVertexFunction(static_cast<MTL::Function*>(sd.vertexFunction));
            desc->setFragmentFunction(static_cast<MTL::Function*>(sd.fragmentFunction));
            desc->setVertexDescriptor(static_cast<MTL::VertexDescriptor*>(sd.vertexDescriptor));
            
            // Match swapchain/render target format
            // TODO: Handle RenderTarget specific formats
            desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
            desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

            if (state.blendEnabled) {
                MTL::RenderPipelineColorAttachmentDescriptor* ca = desc->colorAttachments()->object(0);
                ca->setBlendingEnabled(true);
                ca->setSourceRGBBlendFactor(convertBlendFactor(state.srcColorBlendFactor));
                ca->setDestinationRGBBlendFactor(convertBlendFactor(state.dstColorBlendFactor));
                ca->setRgbBlendOperation(convertBlendOp(state.colorBlendOp));
                ca->setSourceAlphaBlendFactor(convertBlendFactor(state.srcAlphaBlendFactor));
                ca->setDestinationAlphaBlendFactor(convertBlendFactor(state.dstAlphaBlendFactor));
                ca->setAlphaBlendOperation(convertBlendOp(state.alphaBlendOp));
            }

            NS::Error* error = nullptr;
            pipeline = mtlDevice->newRenderPipelineState(desc, &error);
            desc->release();

            if (!pipeline) {
                LOG_ERROR("GHI_Metal", "Failed to create pipeline variant for shader %u: %s", 
                          state.shader.id,
                          error ? error->localizedDescription()->utf8String() : "unknown");
                return;
            }
            pipelineCache_[pipeKey] = pipeline;
            LOG_INFO("GHI_Metal", "Created and cached pipeline variant for shader %u", state.shader.id);
        }
        encoder->setRenderPipelineState(pipeline);

        // 2. Handle Depth Stencil State
        DepthKey depthKey;
        depthKey.depthTestEnabled = state.depthTestEnabled;
        depthKey.depthWriteEnabled = state.depthWriteEnabled;
        depthKey.depthCompareOp = state.depthCompareOp;

        auto depthIt = depthStencilStateCache_.find(depthKey);
        MTL::DepthStencilState* depthState = nullptr;

        if (depthIt != depthStencilStateCache_.end()) {
            depthState = static_cast<MTL::DepthStencilState*>(depthIt->second);
        } else {
            MTL::DepthStencilDescriptor* desc = MTL::DepthStencilDescriptor::alloc()->init();
            desc->setDepthWriteEnabled(state.depthWriteEnabled);
            if (state.depthTestEnabled) {
                desc->setDepthCompareFunction(convertCompareOp(state.depthCompareOp));
            } else {
                desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
            }
            depthState = mtlDevice->newDepthStencilState(desc);
            desc->release();
            depthStencilStateCache_[depthKey] = depthState;
            LOG_INFO("GHI_Metal", "Created and cached depth stencil state");
        }
        encoder->setDepthStencilState(depthState);

        // 3. Handle Culling
        if (state.cullFaceEnabled) {
            switch (state.cullMode) {
                case CullMode::None: encoder->setCullMode(MTL::CullModeNone); break;
                case CullMode::Front: encoder->setCullMode(MTL::CullModeFront); break;
                case CullMode::Back: encoder->setCullMode(MTL::CullModeBack); break;
                case CullMode::FrontAndBack: encoder->setCullMode(MTL::CullModeBack); break; // Best effort
            }
        } else {
            encoder->setCullMode(MTL::CullModeNone);
        }

        switch (state.frontFace) {
            case FrontFace::Clockwise: encoder->setFrontFacingWinding(MTL::WindingClockwise); break;
            case FrontFace::CounterClockwise: encoder->setFrontFacingWinding(MTL::WindingCounterClockwise); break;
        }
    }
}

void GHI_MetalBackend::getRenderState(RenderState& state) {
    state = currentState_;
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter

