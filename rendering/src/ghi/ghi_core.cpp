/**
 * @file ghi_core.cpp
 * @brief GHI Core Implementation
 * 
 * Manages backend selection, resource handle pools, and dispatches to active backend.
 */

#include "rendering/ghi/ghi.h"
#include "rendering/ghi/ighi_backend.h"
#include "logging/logging.h"
#include <memory>
#include <vector>
#include <unordered_map>

// SDL for window integration
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_metal.h>

// Include backend implementations
#include "backends/ghi_vulkan.h"

#ifdef __APPLE__
#include "backends/ghi_metal.h"
// OpenGL is deprecated on macOS (limited to 4.1) - use Metal instead
#else
// OpenGL 4.3+ fallback for Windows/Linux
#include "backends/ghi_opengl.h"
#endif

#ifdef _WIN32
#include "backends/ghi_dx12.h"
#endif

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Global State
// ============================================================================

static std::unique_ptr<IGHIBackend> g_activeBackend = nullptr;
static Backend g_backendType = Backend::Vulkan;
static Capabilities g_capabilities;

// Resource handle generation (simple incrementing IDs)
static uint32_t g_nextBufferID = 1;
static uint32_t g_nextTextureID = 1;
static uint32_t g_nextShaderID = 1;

// ============================================================================
// Initialization
// ============================================================================

bool initialize(Backend backend) {
    LOG_INFO("GHI", "Initializing GHI with backend: %s", getBackendName(backend));
    
    if (g_activeBackend) {
        LOG_WARN("GHI", "GHI already initialized, shutting down previous backend");
        shutdown();
    }
    
    g_backendType = backend;
    
    // Create backend implementation
    switch (backend) {
        case Backend::Vulkan:
            g_activeBackend = std::make_unique<GHI_VulkanBackend>();
            break;
            
        case Backend::Metal:
#ifdef __APPLE__
            g_activeBackend = std::make_unique<GHI_MetalBackend>();
#else
            LOG_ERROR("GHI", "Metal backend only available on macOS");
            return false;
#endif
            break;
            
        case Backend::OpenGL:
#ifndef __APPLE__
            LOG_INFO("GHI", "Creating OpenGL 4.3+ backend");
            g_activeBackend = std::make_unique<GHI_OpenGLBackend>();
            break;
#else
            LOG_ERROR("GHI", "OpenGL backend not available on macOS - use Metal instead");
            return false;
#endif
            
        case Backend::DX12:
#ifdef _WIN32
            LOG_INFO("GHI", "Creating DirectX 12 backend");
            g_activeBackend = std::make_unique<GHI_DX12Backend>();
            break;
#else
            LOG_ERROR("GHI", "DX12 backend only available on Windows");
            return false;
#endif
            
        default:
            LOG_ERROR("GHI", "Unknown backend type");
            return false;
    }
    
    if (!g_activeBackend->initialize()) {
        LOG_ERROR("GHI", "Failed to initialize backend");
        g_activeBackend.reset();
        return false;
    }
    
    // Cache capabilities
    g_capabilities = g_activeBackend->getCapabilities();
    
    LOG_INFO("GHI", "GHI initialized successfully");
    LOG_INFO("GHI", "  Device: %s", g_capabilities.deviceName.c_str());
    LOG_INFO("GHI", "  Compute shaders: %s", g_capabilities.hasComputeShaders ? "yes" : "no");
    LOG_INFO("GHI", "  Indirect draw: %s", g_capabilities.hasIndirectDraw ? "yes" : "no");
    LOG_INFO("GHI", "  Subgroups: %s", g_capabilities.hasSubgroups ? "yes" : "no");
    
    return true;
}

// Global window handle for backends that need it
static void* g_windowHandle = nullptr;

bool initialize(Backend backend, void* window) {
    LOG_INFO("GHI", "Initializing GHI with backend: %s (with window)", getBackendName(backend));
    
    if (g_activeBackend) {
        LOG_WARN("GHI", "GHI already initialized, shutting down previous backend");
        shutdown();
    }
    
    g_backendType = backend;
    g_windowHandle = window;
    
    // Create backend implementation
    switch (backend) {
        case Backend::Vulkan: {
            auto vulkanBackend = std::make_unique<GHI_VulkanBackend>();
            
            // Initialize core Vulkan first
            if (!vulkanBackend->initialize()) {
                LOG_ERROR("GHI", "Failed to initialize Vulkan backend core");
                return false;
            }
            
            // Create surface from window and set up swapchain
            if (window) {
                SDL_Window* sdlWindow = static_cast<SDL_Window*>(window);
                VkSurfaceKHR surface = VK_NULL_HANDLE;
                
                if (!SDL_Vulkan_CreateSurface(sdlWindow, vulkanBackend->getInstance(), nullptr, &surface)) {
                    LOG_ERROR("GHI", "Failed to create Vulkan surface: %s", SDL_GetError());
                    return false;
                }
                
                int w, h;
                SDL_GetWindowSizeInPixels(sdlWindow, &w, &h);
                vulkanBackend->setSurface(surface, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }
            
            g_activeBackend = std::move(vulkanBackend);
            break;
        }
        
        case Backend::Metal:
#ifdef __APPLE__
        {
            auto metalBackend = std::make_unique<GHI_MetalBackend>();
            
            if (!metalBackend->initialize()) {
                LOG_ERROR("GHI", "Failed to initialize Metal backend core");
                return false;
            }
            
            // Set up metal layer from window
            if (window) {
                SDL_Window* sdlWindow = static_cast<SDL_Window*>(window);
                void* metalLayer = SDL_Metal_GetLayer(SDL_Metal_CreateView(sdlWindow));
                if (metalLayer) {
                    metalBackend->setMetalLayer(metalLayer);
                    
                    int w, h;
                    SDL_GetWindowSizeInPixels(sdlWindow, &w, &h);
                    metalBackend->setDrawableSize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                }
            }
            
            g_activeBackend = std::move(metalBackend);
            break;
        }
#else
            LOG_ERROR("GHI", "Metal backend only available on macOS");
            return false;
#endif
        
        case Backend::OpenGL:
#ifndef __APPLE__
        {
            auto openglBackend = std::make_unique<GHI_OpenGLBackend>();
            
            // OpenGL needs the window for context creation
            if (!openglBackend->initializeWithWindow(window)) {
                LOG_ERROR("GHI", "Failed to initialize OpenGL backend");
                return false;
            }
            
            g_activeBackend = std::move(openglBackend);
            break;
        }
#else
            LOG_ERROR("GHI", "OpenGL backend not available on macOS - use Metal instead");
            return false;
#endif
        
        case Backend::DX12:
#ifdef _WIN32
        {
            auto dx12Backend = std::make_unique<GHI_DX12Backend>();
            
            if (!dx12Backend->initializeWithWindow(window)) {
                LOG_ERROR("GHI", "Failed to initialize DX12 backend");
                return false;
            }
            
            g_activeBackend = std::move(dx12Backend);
            break;
        }
#else
            LOG_ERROR("GHI", "DX12 backend only available on Windows");
            return false;
#endif
        
        default:
            LOG_ERROR("GHI", "Unknown backend type");
            return false;
    }
    
    // Cache capabilities
    g_capabilities = g_activeBackend->getCapabilities();
    
    LOG_INFO("GHI", "GHI initialized successfully (with window)");
    LOG_INFO("GHI", "  Device: %s", g_capabilities.deviceName.c_str());
    LOG_INFO("GHI", "  Compute shaders: %s", g_capabilities.hasComputeShaders ? "yes" : "no");
    LOG_INFO("GHI", "  Indirect draw: %s", g_capabilities.hasIndirectDraw ? "yes" : "no");
    
    return true;
}

void shutdown() {
    if (!g_activeBackend) {
        return;
    }
    
    LOG_INFO("GHI", "Shutting down GHI");
    g_activeBackend->shutdown();
    g_activeBackend.reset();
}

Backend getActiveBackend() {
    return g_backendType;
}

bool isInitialized() {
    return g_activeBackend != nullptr;
}

// ============================================================================
// Capabilities
// ============================================================================

const Capabilities& getCapabilities() {
    return g_capabilities;
}

bool hasComputeShaders() {
    return g_capabilities.hasComputeShaders;
}

bool hasIndirectDraw() {
    return g_capabilities.hasIndirectDraw;
}

bool hasStorageBuffers() {
    return g_capabilities.hasStorageBuffers;
}

bool hasSubgroups() {
    return g_capabilities.hasSubgroups;
}

bool hasRayTracing() {
    return g_capabilities.hasRayTracing;
}

// ============================================================================
// Resource Creation
// ============================================================================

BufferHandle createBuffer(const BufferCreateInfo& info) {
    if (!g_activeBackend) {
        LOG_ERROR("GHI", "Cannot create buffer: GHI not initialized");
        return BufferHandle{};
    }
    return g_activeBackend->createBuffer(info);
}

void destroyBuffer(BufferHandle handle) {
    if (!g_activeBackend || !handle.isValid()) return;
    g_activeBackend->destroyBuffer(handle);
}

void updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    if (!g_activeBackend || !handle.isValid()) return;
    g_activeBackend->updateBuffer(handle, offset, size, data);
}

TextureHandle createTexture(const TextureCreateInfo& info) {
    if (!g_activeBackend) {
        LOG_ERROR("GHI", "Cannot create texture: GHI not initialized");
        return TextureHandle{};
    }
    return g_activeBackend->createTexture(info);
}

TextureHandle createTextureFromFile(const char* filepath, bool generateMipmaps) {
    // TODO: Load image using stb_image, then create texture
    LOG_ERROR("GHI", "createTextureFromFile not yet implemented");
    return TextureHandle{};
}

void destroyTexture(TextureHandle handle) {
    if (!g_activeBackend || !handle.isValid()) return;
    g_activeBackend->destroyTexture(handle);
}

ShaderHandle createShader(const ShaderSource& source) {
    if (!g_activeBackend) {
        LOG_ERROR("GHI", "Cannot create shader: GHI not initialized");
        return ShaderHandle{};
    }
    return g_activeBackend->createShader(source);
}

void destroyShader(ShaderHandle handle) {
    if (!g_activeBackend || !handle.isValid()) return;
    g_activeBackend->destroyShader(handle);
}

// ============================================================================
// Sampler Management
// ============================================================================

SamplerHandle createSampler(const SamplerCreateInfo& info) {
    if (!g_activeBackend) {
        LOG_ERROR("GHI", "Cannot create sampler: GHI not initialized");
        return SamplerHandle{};
    }
    return g_activeBackend->createSampler(info);
}

void destroySampler(SamplerHandle handle) {
    if (!g_activeBackend || !handle.isValid()) return;
    g_activeBackend->destroySampler(handle);
}

void bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) {
    if (!g_activeBackend || !sampler.isValid()) return;
    g_activeBackend->bindSampler(sampler, set, binding);
}

// ============================================================================
// Frame Management
// ============================================================================

bool beginFrame() {
    if (!g_activeBackend) return false;
    g_activeBackend->beginFrame();
    return true;
}

void endFrame() {
    if (!g_activeBackend) return;
    g_activeBackend->endFrame();
}

void beginRenderPass() {
    if (!g_activeBackend) return;
    g_activeBackend->beginRenderPass();
}

void beginRenderPass(RenderTargetHandle target) {
    if (!g_activeBackend) return;
    g_activeBackend->beginRenderPass(target);
}

void endRenderPass() {
    if (!g_activeBackend) return;
    g_activeBackend->endRenderPass();
}

// ============================================================================
// Render Target Management
// ============================================================================

RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo& info) {
    if (!g_activeBackend) {
        LOG_ERROR("GHI", "Cannot create render target: GHI not initialized");
        return RenderTargetHandle{};
    }
    return g_activeBackend->createRenderTarget(info);
}

void destroyRenderTarget(RenderTargetHandle handle) {
    if (!g_activeBackend || !handle.isValid()) return;
    g_activeBackend->destroyRenderTarget(handle);
}

TextureHandle getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) {
    if (!g_activeBackend || !target.isValid()) return TextureHandle{};
    return g_activeBackend->getRenderTargetColorTexture(target, index);
}

TextureHandle getRenderTargetDepthTexture(RenderTargetHandle target) {
    if (!g_activeBackend || !target.isValid()) return TextureHandle{};
    return g_activeBackend->getRenderTargetDepthTexture(target);
}

void resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) {
    if (!g_activeBackend || !target.isValid()) return;
    g_activeBackend->resizeRenderTarget(target, width, height);
}

// ============================================================================
// State Management
// ============================================================================

void setRenderState(const RenderState& state) {
    if (!g_activeBackend) return;
    g_activeBackend->setRenderState(state);
}

void getRenderState(RenderState& state) {
    if (!g_activeBackend) return;
    g_activeBackend->getRenderState(state);
}

void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!g_activeBackend) return;
    g_activeBackend->setViewport(x, y, width, height);
}

void setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!g_activeBackend) return;
    g_activeBackend->setScissor(x, y, width, height);
}

// ============================================================================
// Resource Binding
// ============================================================================

void bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) {
    if (!g_activeBackend) return;
    g_activeBackend->bindVertexBuffer(buffer, binding, offset);
}

void bindIndexBuffer(BufferHandle buffer, size_t offset) {
    if (!g_activeBackend) return;
    g_activeBackend->bindIndexBuffer(buffer, offset);
}

void bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    if (!g_activeBackend) return;
    g_activeBackend->bindUniformBuffer(buffer, set, binding);
}

void bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    if (!g_activeBackend) return;
    if (!hasStorageBuffers()) {
        LOG_WARN("GHI", "Storage buffers not supported on this backend");
        return;
    }
    g_activeBackend->bindStorageBuffer(buffer, set, binding);
}

void setPushConstants(const void* data, uint32_t size, uint32_t offset) {
    if (!g_activeBackend) return;
    g_activeBackend->setPushConstants(data, size, offset);
}

void bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    if (!g_activeBackend) return;
    g_activeBackend->bindTexture(texture, set, binding);
}

void bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    if (!g_activeBackend) return;
    if (!hasComputeShaders()) {
        LOG_WARN("GHI", "Storage textures not supported on this backend");
        return;
    }
    g_activeBackend->bindStorageTexture(texture, set, binding);
}

// ============================================================================
// Drawing
// ============================================================================

void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (!g_activeBackend) return;
    g_activeBackend->draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, 
                 int32_t vertexOffset, uint32_t firstInstance) {
    if (!g_activeBackend) return;
    g_activeBackend->drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!g_activeBackend) return;
    
    if (!hasIndirectDraw()) {
        LOG_WARN("GHI", "Indirect draw not supported on this backend");
        return;
    }
    
    g_activeBackend->drawIndirect(indirectBuffer, drawCount, stride);
}

void drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!g_activeBackend) return;
    
    if (!hasIndirectDraw()) {
        LOG_WARN("GHI", "Indirect draw not supported on this backend");
        return;
    }
    
    g_activeBackend->drawIndexedIndirect(indirectBuffer, drawCount, stride);
}

// ============================================================================
// Compute
// ============================================================================

ShaderHandle createComputeShader(const ShaderSource& source) {
    if (!g_activeBackend) return ShaderHandle{};
    
    if (!hasComputeShaders()) {
        LOG_WARN("GHI", "Compute shaders not supported on this backend");
        return ShaderHandle{};
    }
    
    return g_activeBackend->createComputeShader(source);
}

void bindComputeShader(ShaderHandle shader) {
    if (!g_activeBackend) return;
    
    if (!hasComputeShaders()) {
        LOG_WARN("GHI", "Compute shaders not supported on this backend");
        return;
    }
    
    g_activeBackend->bindComputeShader(shader);
}

void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (!g_activeBackend) return;
    
    if (!hasComputeShaders()) {
        LOG_WARN("GHI", "Compute shaders not supported on this backend");
        return;
    }
    
    g_activeBackend->dispatch(groupCountX, groupCountY, groupCountZ);
}

void dispatchIndirect(BufferHandle indirectBuffer) {
    if (!g_activeBackend) return;
    
    if (!hasComputeShaders() || !hasIndirectDraw()) {
        LOG_WARN("GHI", "Indirect compute not supported on this backend");
        return;
    }
    
    g_activeBackend->dispatchIndirect(indirectBuffer);
}

// ============================================================================
// Synchronization
// ============================================================================

void memoryBarrier() {
    if (!g_activeBackend) return;
    g_activeBackend->memoryBarrier();
}

void bufferBarrier(BufferHandle buffer) {
    if (!g_activeBackend) return;
    g_activeBackend->bufferBarrier(buffer);
}

void textureBarrier(TextureHandle texture) {
    if (!g_activeBackend) return;
    g_activeBackend->textureBarrier(texture);
}

// ============================================================================
// Debug
// ============================================================================

void pushDebugGroup(const char* name) {
    if (!g_activeBackend) return;
    // Backend-specific implementation
}

void popDebugGroup() {
    if (!g_activeBackend) return;
    // Backend-specific implementation
}

void setDebugName(BufferHandle handle, const char* name) {
    // Backend-specific implementation
}

void setDebugName(TextureHandle handle, const char* name) {
    // Backend-specific implementation
}

// ============================================================================
// Utility
// ============================================================================

void waitIdle() {
    if (!g_activeBackend) return;
    g_activeBackend->waitIdle();
}

const char* getBackendName(Backend backend) {
    switch (backend) {
        case Backend::Vulkan: return "Vulkan";
        case Backend::Metal: return "Metal";
        case Backend::OpenGL: return "OpenGL";
        case Backend::DX12: return "DirectX 12";
        default: return "Unknown";
    }
}

#ifdef __APPLE__
void setMetalLayer(void* layer) {
    if (!g_activeBackend) {
        LOG_ERROR("GHI", "Cannot set Metal layer: GHI not initialized");
        return;
    }
    
    if (g_backendType != Backend::Metal) {
        LOG_ERROR("GHI", "Cannot set Metal layer: active backend is not Metal");
        return;
    }
    
    // Cast to Metal backend and set layer
    GHI_MetalBackend* metalBackend = static_cast<GHI_MetalBackend*>(g_activeBackend.get());
    metalBackend->setMetalLayer(layer);
}

void setMetalDrawableSize(uint32_t width, uint32_t height) {
    if (!g_activeBackend || g_backendType != Backend::Metal) {
        return;
    }
    
    GHI_MetalBackend* metalBackend = static_cast<GHI_MetalBackend*>(g_activeBackend.get());
    metalBackend->setDrawableSize(width, height);
}
#endif

void* getVulkanInstance() {
    if (!g_activeBackend || g_backendType != Backend::Vulkan) {
        return nullptr;
    }
    GHI_VulkanBackend* vulkanBackend = static_cast<GHI_VulkanBackend*>(g_activeBackend.get());
    return static_cast<void*>(vulkanBackend->getInstance());
}

void setVulkanSurface(void* surface, uint32_t width, uint32_t height) {
    if (!g_activeBackend || g_backendType != Backend::Vulkan) return;
    
    GHI_VulkanBackend* vulkanBackend = static_cast<GHI_VulkanBackend*>(g_activeBackend.get());
    vulkanBackend->setSurface(static_cast<VkSurfaceKHR>(surface), width, height);
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter

