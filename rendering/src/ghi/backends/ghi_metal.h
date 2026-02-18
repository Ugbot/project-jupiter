#pragma once

/**
 * @file ghi_metal.h
 * @brief GHI Metal Backend Implementation
 * 
 * Native Metal backend for macOS/iOS.
 * PRIMARY production backend for Apple platforms (not MoltenVK).
 * 
 * Based on Apple Metal samples:
 * - Deferred lighting (tile shaders, memoryless textures)
 * - Argument buffers (resource management)
 * - Fast resource loading
 */

#include "rendering/ghi/ighi_backend.h"

// Note: We don't include Metal headers in .h file
// All Metal types are opaque void* pointers
// Actual metal-cpp includes are only in .cpp implementation files

#include <unordered_map>
#include <memory>

namespace jupiter {
namespace rendering {
namespace ghi {

/**
 * @brief Metal backend implementation for Apple platforms
 * 
 * Primary macOS/iOS backend. Uses native Metal API for best performance.
 * 
 * Features:
 * - Argument buffers for efficient resource binding
 * - Tile shaders for optimized deferred rendering
 * - Memoryless textures for G-Buffer (huge memory savings)
 * - SIMD-groups for compute (subgroup equivalent)
 * - Fast resource loading
 * 
 * Based on Apple sample code patterns.
 */
class GHI_MetalBackend : public IGHIBackend {
public:
    GHI_MetalBackend();
    ~GHI_MetalBackend() override;

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
    Backend getBackendType() const override { return Backend::Metal; }

    // Metal-specific extensions
    struct MetalExtensions {
        // Tile shaders (for optimized deferred rendering)
        void (*beginTilePass)(void) = nullptr;
        void (*endTilePass)(void) = nullptr;
        
        // Argument buffers (for efficient resource binding)
        void (*useArgumentBuffer)(uint32_t index, void* argumentBuffer) = nullptr;
        
        // SIMD-group operations (for compute optimization)
        bool hasSIMDPermute = false;
        bool hasSIMDShuffle = false;
    };
    
    const MetalExtensions& getMetalExtensions() const { return metalExtensions_; }
    
    // Window integration (called from platform layer)
    void setMetalLayer(void* layer);  // CA::MetalLayer*
    void setDrawableSize(uint32_t width, uint32_t height);

private:
    // Metal device and queues (opaque pointers to metal-cpp objects)
    void* device_ = nullptr;                    // MTL::Device*
    void* commandQueue_ = nullptr;              // MTL::CommandQueue*
    void* currentCommandBuffer_ = nullptr;      // MTL::CommandBuffer*
    void* currentRenderEncoder_ = nullptr;      // MTL::RenderCommandEncoder*
    void* currentComputeEncoder_ = nullptr;     // MTL::ComputeCommandEncoder*
    void* metalLayer_ = nullptr;                // CA::MetalLayer*
    void* currentRenderPassDescriptor_ = nullptr; // MTL::RenderPassDescriptor*

    // Resource tracking
    uint32_t nextBufferID_ = 1;
    uint32_t nextTextureID_ = 1;
    uint32_t nextShaderID_ = 1;
    uint32_t nextRenderTargetID_ = 1;
    uint32_t nextSamplerID_ = 1;
    
    // Standalone samplers
    std::unordered_map<uint32_t, void*> standaloneSamplers_;  // MTL::SamplerState*

    std::unordered_map<uint32_t, void*> buffers_;           // MTL::Buffer*
    struct MetalTextureData {
        void* texture = nullptr; // MTL::Texture*
        void* sampler = nullptr; // MTL::SamplerState*
    };
    std::unordered_map<uint32_t, MetalTextureData> textures_;
    std::unordered_map<uint32_t, void*> computePipelines_;  // MTL::ComputePipelineState*
    std::unordered_map<uint32_t, void*> samplers_;          // MTL::SamplerState*

    // Shader data for pipeline recreation
    struct MetalShaderData {
        void* vertexFunction = nullptr;    // MTL::Function*
        void* fragmentFunction = nullptr;  // MTL::Function*
        void* vertexDescriptor = nullptr;  // MTL::VertexDescriptor*
    };
    std::unordered_map<uint32_t, MetalShaderData> shaders_;

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
    std::unordered_map<PipelineKey, void*, PipelineKeyHash> pipelineCache_; // MTL::RenderPipelineState*

    // Depth state cache
    struct DepthKey {
        bool depthTestEnabled;
        bool depthWriteEnabled;
        CompareOp depthCompareOp;

        bool operator==(const DepthKey& other) const {
            return depthTestEnabled == other.depthTestEnabled &&
                   depthWriteEnabled == other.depthWriteEnabled &&
                   depthCompareOp == other.depthCompareOp;
        }
    };

    struct DepthKeyHash {
        size_t operator()(const DepthKey& k) const {
            size_t h = std::hash<bool>{}(k.depthTestEnabled);
            h ^= std::hash<bool>{}(k.depthWriteEnabled) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(static_cast<int>(k.depthCompareOp)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<DepthKey, void*, DepthKeyHash> depthStencilStateCache_; // MTL::DepthStencilState*
    
    // Render target data
    struct MetalRenderTargetData {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<TextureHandle> colorTextures;
        TextureHandle depthTexture;
        std::vector<glm::vec4> clearColors;
        float depthClearValue = 1.0f;
    };
    std::unordered_map<uint32_t, MetalRenderTargetData> renderTargets_;
    RenderTargetHandle currentRenderTarget_;

    // Current state
    Capabilities capabilities_;
    RenderState currentState_;
    MetalExtensions metalExtensions_;

    // Current drawable
    void* currentDrawable_ = nullptr;  // id<CAMetalDrawable>
    
    // Autorelease pool for current frame
    void* frameAutoreleasePool_ = nullptr;  // NS::AutoreleasePool*
    
    // Bound resources for draw calls
    BufferHandle boundIndexBuffer_;
    size_t boundIndexBufferOffset_ = 0;
    
    // Depth buffer resources
    void* depthTexture_ = nullptr;        // MTL::Texture*
    void* depthStencilState_ = nullptr;   // MTL::DepthStencilState*
    uint32_t depthTextureWidth_ = 0;
    uint32_t depthTextureHeight_ = 0;

    // Helpers (implemented in .mm file)
    void queryCapabilities();
    int convertFormat(Format format);  // Format → MTLPixelFormat
    int convertPrimitiveTopology(PrimitiveTopology topology);  // → MTLPrimitiveType
};

} // namespace ghi
} // namespace rendering
} // namespace jupiter

