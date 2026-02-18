#pragma once

/**
 * @file ghi_opengl.h
 * @brief GHI OpenGL 4.3+ Backend
 * 
 * OpenGL backend for fallback support on older hardware.
 * Targets OpenGL 4.3+ for compute shader support.
 * 
 * Graceful degradation for OpenGL 4.1 (macOS legacy):
 * - No compute shaders
 * - No storage buffers
 * - Limited texture formats
 */

#include "rendering/ghi/ighi_backend.h"
#include <unordered_map>
#include <vector>
#include <string>

// Forward declare OpenGL types
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;

namespace jupiter {
namespace rendering {
namespace ghi {

/**
 * @brief OpenGL 4.3+ backend implementation
 */
class GHI_OpenGLBackend : public IGHIBackend {
public:
    GHI_OpenGLBackend();
    ~GHI_OpenGLBackend() override;

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
    Backend getBackendType() const override { return Backend::OpenGL; }
    
    // Debug (non-interface helpers)
    void setDebugName(BufferHandle buffer, const char* name);
    void setDebugName(TextureHandle texture, const char* name);
    void pushDebugGroup(const char* name);
    void popDebugGroup();

private:
    bool initialized_ = false;
    void* windowHandle_ = nullptr;
    void* glContext_ = nullptr;
    
    // OpenGL version info
    int glMajorVersion_ = 0;
    int glMinorVersion_ = 0;
    bool hasComputeShaders_ = false;
    bool hasSSBO_ = false;
    bool hasIndirectDraw_ = false;
    
    // Framebuffer size
    uint32_t framebufferWidth_ = 0;
    uint32_t framebufferHeight_ = 0;
    
    // Default VAO (required in core profile)
    GLuint defaultVAO_ = 0;
    
    // Resource tracking
    uint32_t nextBufferID_ = 1;
    uint32_t nextTextureID_ = 1;
    uint32_t nextShaderID_ = 1;
    uint32_t nextSamplerID_ = 1;
    uint32_t nextRenderTargetID_ = 1;
    
    // Buffer data
    struct GLBufferData {
        GLuint glBuffer = 0;
        GLenum target = 0;
        size_t size = 0;
    };
    std::unordered_map<uint32_t, GLBufferData> buffers_;
    
    // Texture data
    struct GLTextureData {
        GLuint glTexture = 0;
        GLenum target = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 0;
        Format format;
    };
    std::unordered_map<uint32_t, GLTextureData> textures_;
    
    // Shader data
    struct GLShaderData {
        GLuint program = 0;
        bool isCompute = false;
    };
    std::unordered_map<uint32_t, GLShaderData> shaders_;
    
    // Sampler data
    std::unordered_map<uint32_t, GLuint> samplers_;
    
    // Render target data
    struct GLRenderTargetData {
        GLuint framebuffer = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<TextureHandle> colorTextures;
        TextureHandle depthTexture;
    };
    std::unordered_map<uint32_t, GLRenderTargetData> renderTargets_;
    
    // Current state
    Capabilities capabilities_;
    RenderState currentState_;
    ShaderHandle currentShader_;
    BufferHandle boundIndexBuffer_;
    RenderTargetHandle currentRenderTarget_;
    
    // Push constant buffer (emulated via UBO)
    GLuint pushConstantBuffer_ = 0;
    
    // Helpers
    void queryCapabilities();
    void bindShader(ShaderHandle shader);
    GLenum convertBufferTarget(BufferType type);
    GLenum convertTextureTarget(TextureType type);
    GLenum convertFormat(Format format, GLenum& internalFormat, GLenum& type);
    GLenum convertFilter(Filter filter);
    GLenum convertWrap(WrapMode mode);
    GLenum convertCompareOp(CompareOp op);
    GLenum convertBlendFactor(BlendFactor factor);
    GLenum convertBlendOp(BlendOp op);
    GLenum convertCullMode(CullMode mode);
    GLenum convertFrontFace(FrontFace face);
    
    bool compileShader(GLuint shader, const char* source, const char* name);
    bool linkProgram(GLuint program, const char* name);
};

} // namespace ghi
} // namespace rendering
} // namespace jupiter

