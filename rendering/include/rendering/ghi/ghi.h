#pragma once

/**
 * @file ghi.h
 * @brief Graphics Hardware Interface - Core API
 * 
 * Cross-platform graphics abstraction layer.
 * Provides backend-agnostic API for Vulkan, Metal, OpenGL, DX12.
 * 
 * Based on Venus GHI patterns, adapted for modern GPUs (near-common denominator).
 * 
 * Usage:
 * @code
 * ghi::initialize(ghi::Backend::Metal);  // or Vulkan, OpenGL
 * auto buffer = ghi::createBuffer({...});
 * auto texture = ghi::createTexture({...});
 * ghi::draw(vertexCount, 1);
 * @endcode
 */

#include "ghi_types.h"
#include <string>

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Initialization / Backend Selection
// ============================================================================

/**
 * @brief Initialize GHI with specified backend
 * 
 * @param backend Graphics backend to use
 * @return true if successful
 */
bool initialize(Backend backend);

/**
 * @brief Initialize GHI with specified backend and window
 * 
 * @param backend Graphics backend to use
 * @param window SDL_Window* (or platform-specific window handle)
 * @return true if successful
 */
bool initialize(Backend backend, void* window);

/**
 * @brief Shutdown GHI and cleanup resources
 */
void shutdown();

/**
 * @brief Get active backend type
 */
Backend getActiveBackend();

/**
 * @brief Check if GHI is initialized
 */
bool isInitialized();

// ============================================================================
// Capability Queries
// ============================================================================

/**
 * @brief Get backend capabilities
 * 
 * Query what features the active backend supports.
 * Use before attempting Tier 2+ features.
 */
const Capabilities& getCapabilities();

// Convenience queries
bool hasComputeShaders();
bool hasIndirectDraw();
bool hasStorageBuffers();
bool hasSubgroups();
bool hasRayTracing();

// ============================================================================
// Resource Creation / Destruction
// ============================================================================

/**
 * @brief Create GPU buffer
 * 
 * @param info Buffer creation parameters
 * @return Buffer handle (check .isValid())
 */
BufferHandle createBuffer(const BufferCreateInfo& info);

/**
 * @brief Destroy GPU buffer
 */
void destroyBuffer(BufferHandle handle);

/**
 * @brief Update buffer data
 * 
 * @param handle Buffer to update
 * @param offset Byte offset into buffer
 * @param size Number of bytes to update
 * @param data Pointer to new data
 */
void updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data);

/**
 * @brief Create GPU texture
 * 
 * @param info Texture creation parameters
 * @return Texture handle (check .isValid())
 */
TextureHandle createTexture(const TextureCreateInfo& info);

/**
 * @brief Create texture from image file
 * 
 * @param filepath Path to image file (PNG, JPG, etc.)
 * @param generateMipmaps Whether to auto-generate mipmaps
 * @return Texture handle (check .isValid())
 */
TextureHandle createTextureFromFile(const char* filepath, bool generateMipmaps = true);

/**
 * @brief Destroy GPU texture
 */
void destroyTexture(TextureHandle handle);

/**
 * @brief Create shader program
 * 
 * Backend automatically selects appropriate shader language:
 * - Vulkan: Loads SPIR-V from .spv files
 * - Metal: Loads .metal files or compiles MSL source
 * - OpenGL: Compiles GLSL source
 * 
 * @param source Shader sources (file paths or source code)
 * @return Shader handle (check .isValid())
 */
ShaderHandle createShader(const ShaderSource& source);

/**
 * @brief Destroy shader program
 */
void destroyShader(ShaderHandle handle);

// ============================================================================
// Sampler Management
// ============================================================================

/**
 * @brief Create a texture sampler
 * 
 * Samplers define how textures are filtered and addressed.
 * Can be shared across multiple textures.
 * 
 * @param info Sampler creation parameters
 * @return Sampler handle
 */
SamplerHandle createSampler(const SamplerCreateInfo& info);

/**
 * @brief Destroy a sampler
 */
void destroySampler(SamplerHandle handle);

/**
 * @brief Bind a sampler to a texture binding point
 * 
 * @param sampler Sampler handle
 * @param set Descriptor set index
 * @param binding Binding within set
 */
void bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding);

// ============================================================================
// Frame Management
// ============================================================================

/**
 * @brief Begin frame rendering
 * 
 * Acquires next swapchain image, begins command recording.
 * 
 * @return true if frame can be rendered (false if window minimized, etc.)
 */
bool beginFrame();

/**
 * @brief End frame rendering
 * 
 * Submits command buffer, presents swapchain image.
 */
void endFrame();

/**
 * @brief Begin render pass to swapchain
 * 
 * Clears framebuffer according to RenderState, begins rendering.
 */
void beginRenderPass();

/**
 * @brief Begin render pass to off-screen render target
 * 
 * @param target Render target to render to
 */
void beginRenderPass(RenderTargetHandle target);

/**
 * @brief End render pass
 */
void endRenderPass();

// ============================================================================
// Render Target Management
// ============================================================================

/**
 * @brief Create an off-screen render target (framebuffer)
 * 
 * Render targets are used for:
 * - Deferred rendering (G-Buffer)
 * - Shadow maps
 * - Post-processing effects
 * - Reflection captures
 * 
 * @param info Render target creation parameters
 * @return Render target handle
 */
RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo& info);

/**
 * @brief Destroy a render target
 */
void destroyRenderTarget(RenderTargetHandle handle);

/**
 * @brief Get color attachment texture from render target
 * 
 * @param target Render target handle
 * @param index Color attachment index
 * @return Texture handle for sampling the attachment
 */
TextureHandle getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index = 0);

/**
 * @brief Get depth attachment texture from render target
 * 
 * @param target Render target handle
 * @return Texture handle for sampling the depth
 */
TextureHandle getRenderTargetDepthTexture(RenderTargetHandle target);

/**
 * @brief Resize an existing render target
 * 
 * @param target Render target handle
 * @param width New width
 * @param height New height
 */
void resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height);

// ============================================================================
// State Management
// ============================================================================

/**
 * @brief Set render state
 * 
 * Configure depth test, blending, culling, etc.
 */
void setRenderState(const RenderState& state);

/**
 * @brief Get current render state
 */
void getRenderState(RenderState& state);

/**
 * @brief Set viewport
 */
void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
 * @brief Set scissor rectangle
 */
void setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// ============================================================================
// Resource Binding
// ============================================================================

/**
 * @brief Bind vertex buffer
 * 
 * @param buffer Vertex buffer handle
 * @param binding Binding point
 * @param offset Byte offset into buffer
 */
void bindVertexBuffer(BufferHandle buffer, uint32_t binding = 0, size_t offset = 0);

/**
 * @brief Bind index buffer
 * 
 * @param buffer Index buffer handle
 * @param offset Byte offset into buffer
 */
void bindIndexBuffer(BufferHandle buffer, size_t offset = 0);

/**
 * @brief Bind uniform/constant buffer
 * 
 * @param buffer Uniform buffer handle
 * @param set Descriptor set index (Vulkan/Metal) or binding point (OpenGL)
 * @param binding Binding within set
 */
void bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding);

/**
 * @brief Bind storage buffer (SSBO) for compute shaders
 * 
 * Requires: hasStorageBuffers() == true
 * 
 * @param buffer Storage buffer handle
 * @param set Descriptor set index
 * @param binding Binding within set
 */
void bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding);

/**
 * @brief Set push constants (fast per-object data like model matrix)
 * 
 * Push constants are the fastest way to pass small amounts of data
 * that change per draw call. Limited to 128 bytes on most hardware.
 * 
 * @param data Pointer to data
 * @param size Size in bytes (typically 64 for mat4)
 * @param offset Offset in push constant range (default 0)
 */
void setPushConstants(const void* data, uint32_t size, uint32_t offset = 0);

/**
 * @brief Bind texture to sampler
 * 
 * @param texture Texture handle
 * @param set Descriptor set index
 * @param binding Binding within set
 */
void bindTexture(TextureHandle texture, uint32_t set, uint32_t binding);

/**
 * @brief Bind storage texture (image) for compute shaders
 * 
 * Requires: hasComputeShaders() == true
 * 
 * @param texture Storage texture handle (must have TextureUsage::Storage)
 * @param set Descriptor set index
 * @param binding Binding within set
 */
void bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding);

// ============================================================================
// Drawing Commands (Tier 1 - All backends)
// ============================================================================

/**
 * @brief Draw vertices
 * 
 * @param vertexCount Number of vertices to draw
 * @param instanceCount Number of instances
 * @param firstVertex Offset to first vertex
 * @param firstInstance Offset to first instance
 */
void draw(uint32_t vertexCount, uint32_t instanceCount = 1, 
          uint32_t firstVertex = 0, uint32_t firstInstance = 0);

/**
 * @brief Draw indexed vertices
 * 
 * @param indexCount Number of indices to draw
 * @param instanceCount Number of instances
 * @param firstIndex Offset to first index
 * @param vertexOffset Offset added to vertex indices
 * @param firstInstance Offset to first instance
 */
void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                 uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0);

// ============================================================================
// Advanced Drawing (Tier 2 - Near-common)
// ============================================================================

/**
 * @brief Draw using indirect buffer (GPU-driven)
 * 
 * Requires: hasIndirectDraw() == true
 * 
 * @param indirectBuffer Buffer containing draw commands
 * @param drawCount Number of draw commands
 * @param stride Bytes between draw commands
 */
void drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride);

/**
 * @brief Draw indexed using indirect buffer
 */
void drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride);

// ============================================================================
// Compute (Tier 2 - Near-common)
// ============================================================================

/**
 * @brief Create compute shader
 * 
 * Requires: hasComputeShaders() == true
 * 
 * @param source Shader source (use computePath or computeSource)
 * @return Shader handle for compute pipeline
 */
ShaderHandle createComputeShader(const ShaderSource& source);

/**
 * @brief Bind compute shader before dispatch
 * 
 * @param shader Compute shader handle
 */
void bindComputeShader(ShaderHandle shader);

/**
 * @brief Dispatch compute shader
 * 
 * Requires: hasComputeShaders() == true
 * 
 * @param groupCountX Number of workgroups in X
 * @param groupCountY Number of workgroups in Y
 * @param groupCountZ Number of workgroups in Z
 */
void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

/**
 * @brief Dispatch compute using indirect buffer
 * 
 * Requires: hasComputeShaders() && hasIndirectDraw()
 */
void dispatchIndirect(BufferHandle indirectBuffer);

// ============================================================================
// Synchronization
// ============================================================================

/**
 * @brief Insert memory barrier
 * 
 * Ensures previous GPU operations complete before subsequent ones.
 */
void memoryBarrier();

/**
 * @brief Insert buffer barrier
 * 
 * Synchronize buffer access between compute and graphics.
 */
void bufferBarrier(BufferHandle buffer);

/**
 * @brief Insert texture/image barrier
 * 
 * Transition texture layout or sync access.
 */
void textureBarrier(TextureHandle texture);

// ============================================================================
// Debug / Profiling
// ============================================================================

/**
 * @brief Begin debug marker/group
 * 
 * For GPU profilers (RenderDoc, Xcode, etc.)
 */
void pushDebugGroup(const char* name);

/**
 * @brief End debug marker/group
 */
void popDebugGroup();

/**
 * @brief Set debug name for resource
 * 
 * Useful for GPU debuggers.
 */
void setDebugName(BufferHandle handle, const char* name);
void setDebugName(TextureHandle handle, const char* name);

// ============================================================================
// Utility
// ============================================================================

/**
 * @brief Wait for GPU to finish all pending work
 */
void waitIdle();

/**
 * @brief Get backend name as string
 */
const char* getBackendName(Backend backend);

/**
 * @brief Get Vulkan instance (for SDL surface creation)
 * @return VkInstance or nullptr if not Vulkan backend
 */
void* getVulkanInstance();

/**
 * @brief Set Vulkan surface from SDL
 * @param surface VkSurfaceKHR
 * @param width Surface width
 * @param height Surface height
 */
void setVulkanSurface(void* surface, uint32_t width, uint32_t height);

#ifdef __APPLE__
/**
 * @brief Set CAMetalLayer for Metal backend (macOS only)
 * 
 * Must be called after ghi::initialize(Backend::Metal) and before rendering.
 * 
 * @param layer Pointer to CAMetalLayer from SDL_Metal_GetLayer()
 */
void setMetalLayer(void* layer);

/**
 * @brief Set drawable size for Metal backend
 */
void setMetalDrawableSize(uint32_t width, uint32_t height);
#endif

} // namespace ghi
} // namespace rendering
} // namespace jupiter

