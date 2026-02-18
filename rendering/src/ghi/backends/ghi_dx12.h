#pragma once

/**
 * @file ghi_dx12.h
 * @brief GHI DirectX 12 Backend
 * 
 * Native Windows rendering backend using DirectX 12.
 * 
 * Features:
 * - Full DX12 pipeline state objects
 * - Root signature abstraction
 * - Descriptor heap management
 * - Multi-GPU support (future)
 * - DirectX Raytracing ready (future)
 */

#ifdef _WIN32

#include "rendering/ghi/ighi_backend.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

// Forward declarations for D3D12 types
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;
struct IDXGISwapChain3;
struct ID3D12DescriptorHeap;
struct ID3D12RootSignature;
struct ID3D12PipelineState;
struct ID3D12Resource;
struct D3D12MA_Allocator;
struct D3D12MA_Allocation;

namespace jupiter {
namespace rendering {
namespace ghi {

/**
 * @brief DirectX 12 backend implementation
 */
class GHI_DX12Backend : public IGHIBackend {
public:
    GHI_DX12Backend();
    ~GHI_DX12Backend() override;

    // IGHIBackend interface
    bool initialize() override;
    void shutdown() override;
    void waitIdle() override;
    
    // Window initialization (separate from interface)
    bool initializeWithWindow(void* windowHandle);

    // Resource creation
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

    // Resource binding
    void bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset = 0) override;
    void bindIndexBuffer(BufferHandle buffer, size_t offset = 0) override;
    void bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) override;
    void bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) override;
    void bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) override;
    void bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) override;

    void setPushConstants(const void* data, uint32_t size, uint32_t offset = 0) override;

    // Synchronization
    void memoryBarrier() override;
    void bufferBarrier(BufferHandle buffer) override;
    void textureBarrier(TextureHandle texture) override;

    // Capability queries
    const Capabilities& getCapabilities() const override;
    Backend getBackendType() const override { return Backend::DX12; }
    
    // Debug (non-interface helpers)
    void setDebugName(BufferHandle buffer, const char* name);
    void setDebugName(TextureHandle texture, const char* name);
    void pushDebugGroup(const char* name);
    void popDebugGroup();

private:
    bool initialized_ = false;
    void* windowHandle_ = nullptr;
    
    // Frame buffering
    static constexpr uint32_t FRAME_COUNT = 2;
    uint32_t currentFrame_ = 0;
    
    // D3D12 core objects
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* commandQueue_ = nullptr;
    ID3D12CommandAllocator* commandAllocators_[FRAME_COUNT] = {};
    ID3D12GraphicsCommandList* commandList_ = nullptr;
    
    // Synchronization
    ID3D12Fence* fence_ = nullptr;
    uint64_t fenceValues_[FRAME_COUNT] = {};
    void* fenceEvent_ = nullptr;
    
    // Swapchain
    IDXGISwapChain3* swapchain_ = nullptr;
    ID3D12Resource* renderTargets_[FRAME_COUNT] = {};
    ID3D12DescriptorHeap* rtvHeap_ = nullptr;
    uint32_t rtvDescriptorSize_ = 0;
    
    // Depth buffer
    ID3D12Resource* depthBuffer_ = nullptr;
    ID3D12DescriptorHeap* dsvHeap_ = nullptr;
    
    // Shader resource views
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    uint32_t srvDescriptorSize_ = 0;
    uint32_t nextSrvIndex_ = 0;
    
    // Samplers
    ID3D12DescriptorHeap* samplerHeap_ = nullptr;
    uint32_t samplerDescriptorSize_ = 0;
    
    // Root signature
    ID3D12RootSignature* rootSignature_ = nullptr;
    
    // Resource tracking
    uint32_t nextBufferID_ = 1;
    uint32_t nextTextureID_ = 1;
    uint32_t nextShaderID_ = 1;
    uint32_t nextSamplerID_ = 1;
    uint32_t nextRenderTargetID_ = 1;
    
    // Buffer data
    struct DX12BufferData {
        ID3D12Resource* resource = nullptr;
        D3D12MA_Allocation* allocation = nullptr;
        size_t size = 0;
        uint32_t gpuVirtualAddress = 0;
    };
    std::unordered_map<uint32_t, DX12BufferData> buffers_;
    
    // Texture data
    struct DX12TextureData {
        ID3D12Resource* resource = nullptr;
        D3D12MA_Allocation* allocation = nullptr;
        uint32_t srvIndex = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        Format format;
    };
    std::unordered_map<uint32_t, DX12TextureData> textures_;
    
    // Shader/PSO data
    struct DX12ShaderData {
        ID3D12PipelineState* pso = nullptr;
        bool isCompute = false;
    };
    std::unordered_map<uint32_t, DX12ShaderData> shaders_;
    
    // Sampler data
    struct DX12SamplerData {
        uint32_t heapIndex = 0;
    };
    std::unordered_map<uint32_t, DX12SamplerData> samplers_;
    
    // Render target data
    struct DX12RenderTargetData {
        std::vector<TextureHandle> colorTextures;
        TextureHandle depthTexture;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rtvStartIndex = 0;
        uint32_t dsvIndex = 0;
    };
    std::unordered_map<uint32_t, DX12RenderTargetData> customRenderTargets_;
    
    // Current state
    Capabilities capabilities_;
    RenderState currentState_;
    ShaderHandle currentShader_;
    RenderTargetHandle currentRenderTarget_;
    uint32_t framebufferWidth_ = 0;
    uint32_t framebufferHeight_ = 0;
    
    // Helpers
    bool createDevice();
    bool createCommandQueue();
    bool createSwapchain();
    bool createDescriptorHeaps();
    bool createRootSignature();
    bool createDepthBuffer();
    void waitForGpu();
    void moveToNextFrame();
    void queryCapabilities();
    void bindShader(ShaderHandle shader);
    
    // Format conversion
    uint32_t convertFormat(Format format);
    uint32_t convertBlendFactor(BlendFactor factor);
    uint32_t convertBlendOp(BlendOp op);
    uint32_t convertCompareOp(CompareOp op);
    uint32_t convertCullMode(CullMode mode);
};

} // namespace ghi
} // namespace rendering
} // namespace jupiter

#endif // _WIN32

