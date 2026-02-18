#pragma once

/**
 * @file ighi_backend.h
 * @brief GHI Backend Interface
 * 
 * Abstract interface that all GHI backends must implement.
 * Each backend (Vulkan, Metal, OpenGL, DX12) provides concrete implementation.
 */

#include "ghi_types.h"

namespace jupiter {
namespace rendering {
namespace ghi {

/**
 * @brief Interface for GHI backend implementations
 * 
 * Pure virtual interface that defines the contract for all graphics backends.
 * Concrete implementations: GHI_Vulkan, GHI_Metal, GHI_OpenGL, GHI_DX12
 */
class IGHIBackend {
public:
    virtual ~IGHIBackend() = default;

    // ========================================================================
    // Initialization / Cleanup
    // ========================================================================
    
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void waitIdle() = 0;  // Wait for GPU to finish all work
    
    // ========================================================================
    // Resource Creation
    // ========================================================================
    
    virtual BufferHandle createBuffer(const BufferCreateInfo& info) = 0;
    virtual void destroyBuffer(BufferHandle handle) = 0;
    virtual void updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) = 0;
    
    virtual TextureHandle createTexture(const TextureCreateInfo& info) = 0;
    virtual void destroyTexture(TextureHandle handle) = 0;
    virtual void updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y,
                              uint32_t width, uint32_t height, const void* data) = 0;
    
    virtual ShaderHandle createShader(const ShaderSource& source) = 0;
    virtual void destroyShader(ShaderHandle handle) = 0;
    
    virtual SamplerHandle createSampler(const SamplerCreateInfo& info) = 0;
    virtual void destroySampler(SamplerHandle handle) = 0;
    virtual void bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) = 0;
    
    // ========================================================================
    // Command Recording
    // ========================================================================
    
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    
    virtual void beginRenderPass() = 0;
    virtual void beginRenderPass(RenderTargetHandle target) = 0;
    virtual void endRenderPass() = 0;
    
    virtual void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    virtual void setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    
    // ========================================================================
    // Render Targets
    // ========================================================================
    
    virtual RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo& info) = 0;
    virtual void destroyRenderTarget(RenderTargetHandle handle) = 0;
    virtual TextureHandle getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) = 0;
    virtual TextureHandle getRenderTargetDepthTexture(RenderTargetHandle target) = 0;
    virtual void resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) = 0;
    
    // ========================================================================
    // Drawing
    // ========================================================================
    
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) = 0;  // Tier 2
    virtual void drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) = 0;  // Tier 2
    
    // ========================================================================
    // Compute
    // ========================================================================
    
    virtual ShaderHandle createComputeShader(const ShaderSource& source) = 0;  // Tier 2
    virtual void bindComputeShader(ShaderHandle shader) = 0;  // Tier 2
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;  // Tier 2
    virtual void dispatchIndirect(BufferHandle indirectBuffer) = 0;  // Tier 2
    
    // ========================================================================
    // State Management
    // ========================================================================
    
    virtual void setRenderState(const RenderState& state) = 0;
    virtual void getRenderState(RenderState& state) = 0;
    
    virtual void bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, size_t offset) = 0;
    virtual void bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) = 0;
    virtual void bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) = 0;
    virtual void bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) = 0;
    virtual void bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) = 0;
    virtual void setPushConstants(const void* data, uint32_t size, uint32_t offset = 0) = 0;
    
    // ========================================================================
    // Synchronization
    // ========================================================================
    
    virtual void memoryBarrier() = 0;  // Generic memory barrier
    virtual void bufferBarrier(BufferHandle buffer) = 0;
    virtual void textureBarrier(TextureHandle texture) = 0;
    
    // ========================================================================
    // Queries
    // ========================================================================
    
    virtual const Capabilities& getCapabilities() const = 0;
    virtual Backend getBackendType() const = 0;
};

} // namespace ghi
} // namespace rendering
} // namespace jupiter

