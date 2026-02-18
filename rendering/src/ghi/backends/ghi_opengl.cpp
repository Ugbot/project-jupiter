/**
 * @file ghi_opengl.cpp
 * @brief GHI OpenGL 4.3+ Backend Implementation
 * 
 * OpenGL fallback backend for older hardware.
 */

#include "ghi_opengl.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>

// OpenGL headers
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include <GL/glew.h>
#endif

#include <cstring>
#include <fstream>
#include <sstream>

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Constructor / Destructor
// ============================================================================

GHI_OpenGLBackend::GHI_OpenGLBackend() {
    LOG_INFO("GHI_OpenGL", "OpenGL backend created");
}

GHI_OpenGLBackend::~GHI_OpenGLBackend() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool GHI_OpenGLBackend::initialize() {
    LOG_WARN("GHI_OpenGL", "initialize() called without window handle - use initializeWithWindow()");
    return false;
}

bool GHI_OpenGLBackend::initializeWithWindow(void* windowHandle) {
    if (initialized_) {
        LOG_WARN("GHI_OpenGL", "Already initialized");
        return true;
    }
    
    windowHandle_ = windowHandle;
    SDL_Window* window = static_cast<SDL_Window*>(windowHandle);
    
    LOG_INFO("GHI_OpenGL", "Initializing OpenGL backend");
    
    // Request OpenGL context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    // Create context
    glContext_ = SDL_GL_CreateContext(window);
    if (!glContext_) {
        // Try fallback to 4.1 (macOS)
        LOG_WARN("GHI_OpenGL", "Failed to create OpenGL 4.3 context, trying 4.1");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        glContext_ = SDL_GL_CreateContext(window);
        
        if (!glContext_) {
            LOG_ERROR("GHI_OpenGL", "Failed to create OpenGL context: %s", SDL_GetError());
            return false;
        }
    }
    
    SDL_GL_MakeCurrent(window, static_cast<SDL_GLContext>(glContext_));
    
#ifndef __APPLE__
    // Initialize GLEW on non-Apple platforms
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        LOG_ERROR("GHI_OpenGL", "Failed to initialize GLEW: %s", glewGetErrorString(glewErr));
        return false;
    }
#endif
    
    // Get version info
    glGetIntegerv(GL_MAJOR_VERSION, &glMajorVersion_);
    glGetIntegerv(GL_MINOR_VERSION, &glMinorVersion_);
    
    LOG_INFO("GHI_OpenGL", "OpenGL %d.%d initialized", glMajorVersion_, glMinorVersion_);
    LOG_INFO("GHI_OpenGL", "Vendor: %s", glGetString(GL_VENDOR));
    LOG_INFO("GHI_OpenGL", "Renderer: %s", glGetString(GL_RENDERER));
    
    // Check feature support
    hasComputeShaders_ = (glMajorVersion_ > 4) || (glMajorVersion_ == 4 && glMinorVersion_ >= 3);
    hasSSBO_ = hasComputeShaders_;
    hasIndirectDraw_ = (glMajorVersion_ > 4) || (glMajorVersion_ == 4 && glMinorVersion_ >= 0);
    
    LOG_INFO("GHI_OpenGL", "Features: compute=%s, SSBO=%s, indirect=%s",
             hasComputeShaders_ ? "yes" : "no",
             hasSSBO_ ? "yes" : "no",
             hasIndirectDraw_ ? "yes" : "no");
    
    // Create default VAO (required in core profile)
    glGenVertexArrays(1, &defaultVAO_);
    glBindVertexArray(defaultVAO_);
    
    // Get framebuffer size
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    framebufferWidth_ = static_cast<uint32_t>(w);
    framebufferHeight_ = static_cast<uint32_t>(h);
    
    // Create push constant buffer (emulated via UBO)
    glGenBuffers(1, &pushConstantBuffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, pushConstantBuffer_);
    glBufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
    
    // Query capabilities
    queryCapabilities();
    
    // Enable V-Sync
    SDL_GL_SetSwapInterval(1);
    
    initialized_ = true;
    LOG_INFO("GHI_OpenGL", "OpenGL backend initialized successfully");
    return true;
}

void GHI_OpenGLBackend::waitIdle() {
    glFinish();
}

void GHI_OpenGLBackend::shutdown() {
    if (!initialized_) return;
    
    LOG_INFO("GHI_OpenGL", "Shutting down OpenGL backend");
    
    // Destroy buffers
    for (auto& [id, buffer] : buffers_) {
        if (buffer.glBuffer != 0) {
            glDeleteBuffers(1, &buffer.glBuffer);
        }
    }
    buffers_.clear();
    
    // Destroy textures
    for (auto& [id, texture] : textures_) {
        if (texture.glTexture != 0) {
            glDeleteTextures(1, &texture.glTexture);
        }
    }
    textures_.clear();
    
    // Destroy samplers
    for (auto& [id, sampler] : samplers_) {
        if (sampler != 0) {
            glDeleteSamplers(1, &sampler);
        }
    }
    samplers_.clear();
    
    // Destroy shaders
    for (auto& [id, shader] : shaders_) {
        if (shader.program != 0) {
            glDeleteProgram(shader.program);
        }
    }
    shaders_.clear();
    
    // Destroy render targets
    for (auto& [id, rt] : renderTargets_) {
        if (rt.framebuffer != 0) {
            glDeleteFramebuffers(1, &rt.framebuffer);
        }
    }
    renderTargets_.clear();
    
    // Destroy push constant buffer
    if (pushConstantBuffer_ != 0) {
        glDeleteBuffers(1, &pushConstantBuffer_);
        pushConstantBuffer_ = 0;
    }
    
    // Destroy default VAO
    if (defaultVAO_ != 0) {
        glDeleteVertexArrays(1, &defaultVAO_);
        defaultVAO_ = 0;
    }
    
    // Destroy context
    if (glContext_) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = nullptr;
    }
    
    initialized_ = false;
}

void GHI_OpenGLBackend::queryCapabilities() {
    capabilities_.backendType = Backend::OpenGL;
    
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, reinterpret_cast<GLint*>(&capabilities_.maxTextureSize));
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, reinterpret_cast<GLint*>(&capabilities_.maxTextureUnits));
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, reinterpret_cast<GLint*>(&capabilities_.maxUniformBufferBindings));
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, reinterpret_cast<GLint*>(&capabilities_.maxVertexAttributes));
    
    // Check for compute shader support
    if (hasComputeShaders_) {
        GLint maxComputeGroups[3];
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxComputeGroups[0]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &maxComputeGroups[1]);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &maxComputeGroups[2]);
        capabilities_.maxComputeWorkGroupCount[0] = maxComputeGroups[0];
        capabilities_.maxComputeWorkGroupCount[1] = maxComputeGroups[1];
        capabilities_.maxComputeWorkGroupCount[2] = maxComputeGroups[2];
        capabilities_.hasComputeShaders = true;
    }
    
    capabilities_.hasIndirectDraw = hasIndirectDraw_;
    capabilities_.hasStorageBuffers = hasSSBO_;
    capabilities_.hasGeometryShaders = true;
    capabilities_.hasTessellation = (glMajorVersion_ >= 4);
    capabilities_.maxColorAttachments = 8;
    
    LOG_INFO("GHI_OpenGL", "Capabilities: maxTex=%u, maxUniform=%u, compute=%s",
             capabilities_.maxTextureSize, capabilities_.maxUniformBufferBindings,
             capabilities_.hasComputeShaders ? "yes" : "no");
}

// ============================================================================
// Buffer Management
// ============================================================================

BufferHandle GHI_OpenGLBackend::createBuffer(const BufferCreateInfo& info) {
    GLBufferData bufData;
    bufData.target = convertBufferTarget(info.type);
    bufData.size = info.size;
    
    glGenBuffers(1, &bufData.glBuffer);
    glBindBuffer(bufData.target, bufData.glBuffer);
    
    GLenum usage = GL_STATIC_DRAW;
    if (info.usage == BufferUsage::Dynamic) {
        usage = GL_DYNAMIC_DRAW;
    } else if (info.usage == BufferUsage::Stream) {
        usage = GL_STREAM_DRAW;
    }
    
    glBufferData(bufData.target, info.size, info.data, usage);
    
    BufferHandle handle;
    handle.id = nextBufferID_++;
    buffers_[handle.id] = bufData;
    
    LOG_INFO("GHI_OpenGL", "Created buffer: id=%u, size=%zu, target=0x%X",
             handle.id, info.size, bufData.target);
    
    return handle;
}

void GHI_OpenGLBackend::destroyBuffer(BufferHandle handle) {
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end()) {
        glDeleteBuffers(1, &it->second.glBuffer);
        buffers_.erase(it);
    }
}

void GHI_OpenGLBackend::updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    
    glBindBuffer(it->second.target, it->second.glBuffer);
    glBufferSubData(it->second.target, offset, size, data);
}

// ============================================================================
// Texture Management
// ============================================================================

TextureHandle GHI_OpenGLBackend::createTexture(const TextureCreateInfo& info) {
    GLTextureData texData;
    texData.target = convertTextureTarget(info.type);
    texData.width = info.width;
    texData.height = info.height;
    texData.depth = info.depth;
    texData.format = info.format;
    
    glGenTextures(1, &texData.glTexture);
    glBindTexture(texData.target, texData.glTexture);
    
    GLenum internalFormat, dataFormat, dataType;
    dataFormat = convertFormat(info.format, internalFormat, dataType);
    
    if (info.type == TextureType::Texture2D) {
        glTexImage2D(texData.target, 0, internalFormat, info.width, info.height, 0,
                     dataFormat, dataType, info.data);
    } else if (info.type == TextureType::Texture3D) {
        glTexImage3D(texData.target, 0, internalFormat, info.width, info.height, info.depth,
                     0, dataFormat, dataType, info.data);
    } else if (info.type == TextureType::TextureCube) {
        for (int face = 0; face < 6; face++) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, internalFormat,
                        info.width, info.height, 0, dataFormat, dataType, nullptr);
        }
    }
    
    // Generate mipmaps if requested
    if (info.mipLevels > 1 && info.data) {
        glGenerateMipmap(texData.target);
    }
    
    // Set default sampler parameters
    glTexParameteri(texData.target, GL_TEXTURE_MIN_FILTER, convertFilter(info.minFilter));
    glTexParameteri(texData.target, GL_TEXTURE_MAG_FILTER, convertFilter(info.magFilter));
    glTexParameteri(texData.target, GL_TEXTURE_WRAP_S, convertWrap(info.wrapS));
    glTexParameteri(texData.target, GL_TEXTURE_WRAP_T, convertWrap(info.wrapT));
    
    if (info.type == TextureType::Texture3D || info.type == TextureType::TextureCube) {
        glTexParameteri(texData.target, GL_TEXTURE_WRAP_R, convertWrap(info.wrapR));
    }
    
    TextureHandle handle;
    handle.id = nextTextureID_++;
    textures_[handle.id] = texData;
    
    LOG_INFO("GHI_OpenGL", "Created texture: id=%u, size=%ux%u, target=0x%X",
             handle.id, info.width, info.height, texData.target);
    
    return handle;
}

void GHI_OpenGLBackend::destroyTexture(TextureHandle handle) {
    auto it = textures_.find(handle.id);
    if (it != textures_.end()) {
        glDeleteTextures(1, &it->second.glTexture);
        textures_.erase(it);
    }
}

void GHI_OpenGLBackend::updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y,
                                       uint32_t width, uint32_t height, const void* data) {
    auto it = textures_.find(handle.id);
    if (it == textures_.end()) return;
    
    GLenum internalFormat, dataFormat, dataType;
    dataFormat = convertFormat(it->second.format, internalFormat, dataType);
    
    glBindTexture(it->second.target, it->second.glTexture);
    glTexSubImage2D(it->second.target, level, x, y, width, height, dataFormat, dataType, data);
}

// ============================================================================
// Shader Management
// ============================================================================

ShaderHandle GHI_OpenGLBackend::createShader(const ShaderSource& source) {
    GLShaderData shaderData;
    shaderData.isCompute = false;
    
    // Create program
    shaderData.program = glCreateProgram();
    
    GLuint vertShader = 0;
    GLuint fragShader = 0;
    
    // Load vertex shader
    if (source.vertexPath || source.vertexSource) {
        vertShader = glCreateShader(GL_VERTEX_SHADER);
        
        std::string shaderCode;
        if (source.vertexPath) {
            std::ifstream file(source.vertexPath);
            if (file) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderCode = ss.str();
            } else {
                LOG_ERROR("GHI_OpenGL", "Failed to load vertex shader: %s", source.vertexPath);
                glDeleteShader(vertShader);
                glDeleteProgram(shaderData.program);
                return ShaderHandle{};
            }
        } else {
            shaderCode = source.vertexSource;
        }
        
        if (!compileShader(vertShader, shaderCode.c_str(), "vertex")) {
            glDeleteShader(vertShader);
            glDeleteProgram(shaderData.program);
            return ShaderHandle{};
        }
        
        glAttachShader(shaderData.program, vertShader);
    }
    
    // Load fragment shader
    if (source.fragmentPath || source.fragmentSource) {
        fragShader = glCreateShader(GL_FRAGMENT_SHADER);
        
        std::string shaderCode;
        if (source.fragmentPath) {
            std::ifstream file(source.fragmentPath);
            if (file) {
                std::stringstream ss;
                ss << file.rdbuf();
                shaderCode = ss.str();
            } else {
                LOG_ERROR("GHI_OpenGL", "Failed to load fragment shader: %s", source.fragmentPath);
                if (vertShader) glDeleteShader(vertShader);
                glDeleteShader(fragShader);
                glDeleteProgram(shaderData.program);
                return ShaderHandle{};
            }
        } else {
            shaderCode = source.fragmentSource;
        }
        
        if (!compileShader(fragShader, shaderCode.c_str(), "fragment")) {
            if (vertShader) glDeleteShader(vertShader);
            glDeleteShader(fragShader);
            glDeleteProgram(shaderData.program);
            return ShaderHandle{};
        }
        
        glAttachShader(shaderData.program, fragShader);
    }
    
    // Link program
    if (!linkProgram(shaderData.program, "graphics")) {
        if (vertShader) glDeleteShader(vertShader);
        if (fragShader) glDeleteShader(fragShader);
        glDeleteProgram(shaderData.program);
        return ShaderHandle{};
    }
    
    // Cleanup shaders (already attached)
    if (vertShader) glDeleteShader(vertShader);
    if (fragShader) glDeleteShader(fragShader);
    
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    shaders_[handle.id] = shaderData;
    
    LOG_INFO("GHI_OpenGL", "Created shader program: id=%u", handle.id);
    return handle;
}

void GHI_OpenGLBackend::destroyShader(ShaderHandle handle) {
    auto it = shaders_.find(handle.id);
    if (it != shaders_.end()) {
        glDeleteProgram(it->second.program);
        shaders_.erase(it);
    }
}

ShaderHandle GHI_OpenGLBackend::createComputeShader(const ShaderSource& source) {
    if (!hasComputeShaders_) {
        LOG_ERROR("GHI_OpenGL", "Compute shaders not supported on this OpenGL version");
        return ShaderHandle{};
    }
    
    GLShaderData shaderData;
    shaderData.isCompute = true;
    shaderData.program = glCreateProgram();
    
    GLuint compShader = glCreateShader(GL_COMPUTE_SHADER);
    
    std::string shaderCode;
    if (source.computePath) {
        std::ifstream file(source.computePath);
        if (file) {
            std::stringstream ss;
            ss << file.rdbuf();
            shaderCode = ss.str();
        } else {
            LOG_ERROR("GHI_OpenGL", "Failed to load compute shader: %s", source.computePath);
            glDeleteShader(compShader);
            glDeleteProgram(shaderData.program);
            return ShaderHandle{};
        }
    } else if (source.computeSource) {
        shaderCode = source.computeSource;
    }
    
    if (!compileShader(compShader, shaderCode.c_str(), "compute")) {
        glDeleteShader(compShader);
        glDeleteProgram(shaderData.program);
        return ShaderHandle{};
    }
    
    glAttachShader(shaderData.program, compShader);
    
    if (!linkProgram(shaderData.program, "compute")) {
        glDeleteShader(compShader);
        glDeleteProgram(shaderData.program);
        return ShaderHandle{};
    }
    
    glDeleteShader(compShader);
    
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    shaders_[handle.id] = shaderData;
    
    LOG_INFO("GHI_OpenGL", "Created compute shader: id=%u", handle.id);
    return handle;
}

bool GHI_OpenGLBackend::compileShader(GLuint shader, const char* source, const char* name) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        LOG_ERROR("GHI_OpenGL", "Failed to compile %s shader: %s", name, infoLog);
        return false;
    }
    
    return true;
}

bool GHI_OpenGLBackend::linkProgram(GLuint program, const char* name) {
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        LOG_ERROR("GHI_OpenGL", "Failed to link %s program: %s", name, infoLog);
        return false;
    }
    
    return true;
}

// ============================================================================
// Sampler Management
// ============================================================================

SamplerHandle GHI_OpenGLBackend::createSampler(const SamplerCreateInfo& info) {
    GLuint sampler;
    glGenSamplers(1, &sampler);
    
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, convertFilter(info.minFilter));
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, convertFilter(info.magFilter));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, convertWrap(info.wrapS));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, convertWrap(info.wrapT));
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, convertWrap(info.wrapR));
    
    if (info.anisotropyEnabled) {
        glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, info.maxAnisotropy);
    }
    
    glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, info.minLod);
    glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, info.maxLod);
    glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, info.mipLodBias);
    
    if (info.compareEnabled) {
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, convertCompareOp(info.compareOp));
    }
    
    SamplerHandle handle;
    handle.id = nextSamplerID_++;
    samplers_[handle.id] = sampler;
    
    LOG_INFO("GHI_OpenGL", "Created sampler: id=%u", handle.id);
    return handle;
}

void GHI_OpenGLBackend::destroySampler(SamplerHandle handle) {
    auto it = samplers_.find(handle.id);
    if (it != samplers_.end()) {
        glDeleteSamplers(1, &it->second);
        samplers_.erase(it);
    }
}

void GHI_OpenGLBackend::bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) {
    auto it = samplers_.find(sampler.id);
    if (it != samplers_.end()) {
        glBindSampler(binding, it->second);
    }
}

// ============================================================================
// Frame Rendering
// ============================================================================

void GHI_OpenGLBackend::beginFrame() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GHI_OpenGLBackend::endFrame() {
    SDL_Window* window = static_cast<SDL_Window*>(windowHandle_);
    SDL_GL_SwapWindow(window);
}

void GHI_OpenGLBackend::beginRenderPass() {
    // Bind default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Set viewport
    glViewport(0, 0, framebufferWidth_, framebufferHeight_);
    
    // Clear
    glClearColor(currentState_.clearColor.r, currentState_.clearColor.g,
                 currentState_.clearColor.b, currentState_.clearColor.a);
    glClearDepth(currentState_.clearDepth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    currentRenderTarget_ = RenderTargetHandle{};
}

void GHI_OpenGLBackend::beginRenderPass(RenderTargetHandle target) {
    if (!target.isValid()) {
        beginRenderPass();
        return;
    }
    
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) {
        LOG_ERROR("GHI_OpenGL", "Invalid render target: %u", target.id);
        return;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, it->second.framebuffer);
    glViewport(0, 0, it->second.width, it->second.height);
    
    glClearColor(currentState_.clearColor.r, currentState_.clearColor.g,
                 currentState_.clearColor.b, currentState_.clearColor.a);
    glClearDepth(currentState_.clearDepth);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    currentRenderTarget_ = target;
}

void GHI_OpenGLBackend::endRenderPass() {
    // Nothing special needed for OpenGL
    currentRenderTarget_ = RenderTargetHandle{};
}

// ============================================================================
// Render Targets
// ============================================================================

RenderTargetHandle GHI_OpenGLBackend::createRenderTarget(const RenderTargetCreateInfo& info) {
    GLRenderTargetData rtData;
    rtData.width = info.width;
    rtData.height = info.height;
    
    glGenFramebuffers(1, &rtData.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, rtData.framebuffer);
    
    // Create color attachments
    for (size_t i = 0; i < info.colorAttachments.size(); i++) {
        TextureCreateInfo texInfo;
        texInfo.type = TextureType::Texture2D;
        texInfo.format = info.colorAttachments[i].format;
        texInfo.width = info.width;
        texInfo.height = info.height;
        texInfo.mipLevels = 1;
        texInfo.usage = TextureUsage::RenderTarget | TextureUsage::Sampled;
        
        TextureHandle colorTex = createTexture(texInfo);
        rtData.colorTextures.push_back(colorTex);
        
        auto texIt = textures_.find(colorTex.id);
        if (texIt != textures_.end()) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                                   GL_TEXTURE_2D, texIt->second.glTexture, 0);
        }
    }
    
    // Create depth attachment
    if (info.hasDepth) {
        TextureCreateInfo depthInfo;
        depthInfo.type = TextureType::Texture2D;
        depthInfo.format = info.depthFormat;
        depthInfo.width = info.width;
        depthInfo.height = info.height;
        depthInfo.mipLevels = 1;
        depthInfo.usage = TextureUsage::DepthStencil;
        
        TextureHandle depthTex = createTexture(depthInfo);
        rtData.depthTexture = depthTex;
        
        auto texIt = textures_.find(depthTex.id);
        if (texIt != textures_.end()) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_2D, texIt->second.glTexture, 0);
        }
    }
    
    // Check framebuffer completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("GHI_OpenGL", "Framebuffer incomplete: 0x%X", status);
        glDeleteFramebuffers(1, &rtData.framebuffer);
        for (auto& tex : rtData.colorTextures) {
            destroyTexture(tex);
        }
        if (rtData.depthTexture.isValid()) {
            destroyTexture(rtData.depthTexture);
        }
        return RenderTargetHandle{};
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    RenderTargetHandle handle;
    handle.id = nextRenderTargetID_++;
    renderTargets_[handle.id] = rtData;
    
    LOG_INFO("GHI_OpenGL", "Created render target: id=%u, size=%ux%u",
             handle.id, info.width, info.height);
    
    return handle;
}

void GHI_OpenGLBackend::destroyRenderTarget(RenderTargetHandle handle) {
    auto it = renderTargets_.find(handle.id);
    if (it == renderTargets_.end()) return;
    
    for (auto& tex : it->second.colorTextures) {
        destroyTexture(tex);
    }
    if (it->second.depthTexture.isValid()) {
        destroyTexture(it->second.depthTexture);
    }
    
    glDeleteFramebuffers(1, &it->second.framebuffer);
    renderTargets_.erase(it);
}

TextureHandle GHI_OpenGLBackend::getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) {
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end() || index >= it->second.colorTextures.size()) {
        return TextureHandle{};
    }
    return it->second.colorTextures[index];
}

TextureHandle GHI_OpenGLBackend::getRenderTargetDepthTexture(RenderTargetHandle target) {
    auto it = renderTargets_.find(target.id);
    if (it == renderTargets_.end()) return TextureHandle{};
    return it->second.depthTexture;
}

void GHI_OpenGLBackend::resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) {
    LOG_WARN("GHI_OpenGL", "Render target resize not yet implemented");
}

// ============================================================================
// Drawing
// ============================================================================

void GHI_OpenGLBackend::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (instanceCount > 1) {
        glDrawArraysInstancedBaseInstance(GL_TRIANGLES, firstVertex, vertexCount, instanceCount, firstInstance);
    } else {
        glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount);
    }
}

void GHI_OpenGLBackend::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                     int32_t vertexOffset, uint32_t firstInstance) {
    GLenum indexType = GL_UNSIGNED_INT;
    size_t indexOffset = firstIndex * sizeof(uint32_t);
    
    if (instanceCount > 1) {
        glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, indexCount, indexType,
            reinterpret_cast<void*>(indexOffset), instanceCount, vertexOffset, firstInstance);
    } else {
        glDrawElementsBaseVertex(GL_TRIANGLES, indexCount, indexType,
            reinterpret_cast<void*>(indexOffset), vertexOffset);
    }
}

void GHI_OpenGLBackend::drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!hasIndirectDraw_) {
        LOG_ERROR("GHI_OpenGL", "Indirect draw not supported");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) return;
    
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, it->second.glBuffer);
    
    for (uint32_t i = 0; i < drawCount; i++) {
        glDrawArraysIndirect(GL_TRIANGLES, reinterpret_cast<void*>(i * stride));
    }
}

void GHI_OpenGLBackend::drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    if (!hasIndirectDraw_) {
        LOG_ERROR("GHI_OpenGL", "Indirect draw not supported");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) return;
    
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, it->second.glBuffer);
    
    for (uint32_t i = 0; i < drawCount; i++) {
        glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<void*>(i * stride));
    }
}

// ============================================================================
// Compute
// ============================================================================

void GHI_OpenGLBackend::bindComputeShader(ShaderHandle shader) {
    auto it = shaders_.find(shader.id);
    if (it != shaders_.end() && it->second.isCompute) {
        glUseProgram(it->second.program);
        currentShader_ = shader;
    }
}

void GHI_OpenGLBackend::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (!hasComputeShaders_) {
        LOG_ERROR("GHI_OpenGL", "Compute shaders not supported");
        return;
    }
    
    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
}

void GHI_OpenGLBackend::dispatchIndirect(BufferHandle indirectBuffer) {
    if (!hasComputeShaders_) {
        LOG_ERROR("GHI_OpenGL", "Compute shaders not supported");
        return;
    }
    
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) return;
    
    glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, it->second.glBuffer);
    glDispatchComputeIndirect(0);
}

// ============================================================================
// State Management
// ============================================================================

void GHI_OpenGLBackend::setRenderState(const RenderState& state) {
    currentState_ = state;
    
    // Depth testing
    if (state.depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(convertCompareOp(state.depthCompareOp));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    
    glDepthMask(state.depthWriteEnabled ? GL_TRUE : GL_FALSE);
    
    // Blending
    if (state.blendEnabled) {
        glEnable(GL_BLEND);
        // Default blend mode
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    
    // Culling
    if (state.cullFaceEnabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(convertCullMode(state.cullMode));
        glFrontFace(convertFrontFace(state.frontFace));
    } else {
        glDisable(GL_CULL_FACE);
    }
    
    // Bind shader
    if (state.shader.isValid()) {
        bindShader(state.shader);
    }
}

void GHI_OpenGLBackend::getRenderState(RenderState& state) {
    state = currentState_;
}

void GHI_OpenGLBackend::bindShader(ShaderHandle shader) {
    auto it = shaders_.find(shader.id);
    if (it != shaders_.end()) {
        glUseProgram(it->second.program);
        currentShader_ = shader;
    }
}

void GHI_OpenGLBackend::setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    glViewport(x, y, width, height);
}

void GHI_OpenGLBackend::setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

// ============================================================================
// Resource Binding
// ============================================================================

void GHI_OpenGLBackend::bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    glBindBuffer(GL_ARRAY_BUFFER, it->second.glBuffer);
    
    // Setup vertex attributes (assuming standard Vertex3D layout)
    // Position (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(offset));
    
    // Normal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(offset + 12));
    
    // UV (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(offset + 24));
}

void GHI_OpenGLBackend::bindIndexBuffer(BufferHandle buffer, size_t offset) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.glBuffer);
    boundIndexBuffer_ = buffer;
}

void GHI_OpenGLBackend::bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, it->second.glBuffer);
}

void GHI_OpenGLBackend::bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    if (!hasSSBO_) {
        LOG_WARN("GHI_OpenGL", "Storage buffers not supported");
        return;
    }
    
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, it->second.glBuffer);
}

void GHI_OpenGLBackend::bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    auto it = textures_.find(texture.id);
    if (it == textures_.end()) return;
    
    glActiveTexture(GL_TEXTURE0 + binding);
    glBindTexture(it->second.target, it->second.glTexture);
}

void GHI_OpenGLBackend::bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    if (!hasComputeShaders_) {
        LOG_WARN("GHI_OpenGL", "Image load/store not supported");
        return;
    }
    
    auto it = textures_.find(texture.id);
    if (it == textures_.end()) return;
    
    GLenum internalFormat, dataFormat, dataType;
    convertFormat(it->second.format, internalFormat, dataType);
    
    glBindImageTexture(binding, it->second.glTexture, 0, GL_FALSE, 0, GL_READ_WRITE, internalFormat);
}

void GHI_OpenGLBackend::setPushConstants(const void* data, uint32_t size, uint32_t offset) {
    // Emulate push constants via uniform buffer
    glBindBuffer(GL_UNIFORM_BUFFER, pushConstantBuffer_);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
    glBindBufferBase(GL_UNIFORM_BUFFER, 15, pushConstantBuffer_);  // Binding 15 for push constants
}

// ============================================================================
// Synchronization
// ============================================================================

void GHI_OpenGLBackend::memoryBarrier() {
    if (hasComputeShaders_) {
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }
}

void GHI_OpenGLBackend::bufferBarrier(BufferHandle buffer) {
    if (hasSSBO_) {
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }
}

void GHI_OpenGLBackend::textureBarrier(TextureHandle texture) {
    if (hasComputeShaders_) {
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
}

// ============================================================================
// Capabilities
// ============================================================================

const Capabilities& GHI_OpenGLBackend::getCapabilities() const {
    return capabilities_;
}

// ============================================================================
// Debug
// ============================================================================

void GHI_OpenGLBackend::setDebugName(BufferHandle buffer, const char* name) {
    auto it = buffers_.find(buffer.id);
    if (it != buffers_.end()) {
        glObjectLabel(GL_BUFFER, it->second.glBuffer, -1, name);
    }
}

void GHI_OpenGLBackend::setDebugName(TextureHandle texture, const char* name) {
    auto it = textures_.find(texture.id);
    if (it != textures_.end()) {
        glObjectLabel(GL_TEXTURE, it->second.glTexture, -1, name);
    }
}

void GHI_OpenGLBackend::pushDebugGroup(const char* name) {
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
}

void GHI_OpenGLBackend::popDebugGroup() {
    glPopDebugGroup();
}

// ============================================================================
// Format Conversion Helpers
// ============================================================================

GLenum GHI_OpenGLBackend::convertBufferTarget(BufferType type) {
    switch (type) {
        case BufferType::Vertex: return GL_ARRAY_BUFFER;
        case BufferType::Index: return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::Uniform: return GL_UNIFORM_BUFFER;
        case BufferType::Storage: return GL_SHADER_STORAGE_BUFFER;
        case BufferType::Indirect: return GL_DRAW_INDIRECT_BUFFER;
        default: return GL_ARRAY_BUFFER;
    }
}

GLenum GHI_OpenGLBackend::convertTextureTarget(TextureType type) {
    switch (type) {
        case TextureType::Texture2D: return GL_TEXTURE_2D;
        case TextureType::Texture3D: return GL_TEXTURE_3D;
        case TextureType::TextureCube: return GL_TEXTURE_CUBE_MAP;
        case TextureType::TextureArray: return GL_TEXTURE_2D_ARRAY;
        default: return GL_TEXTURE_2D;
    }
}

GLenum GHI_OpenGLBackend::convertFormat(Format format, GLenum& internalFormat, GLenum& type) {
    switch (format) {
        case Format::R8_UNORM:
            internalFormat = GL_R8;
            type = GL_UNSIGNED_BYTE;
            return GL_RED;
        case Format::RG8_UNORM:
            internalFormat = GL_RG8;
            type = GL_UNSIGNED_BYTE;
            return GL_RG;
        case Format::RGB8_UNORM:
            internalFormat = GL_RGB8;
            type = GL_UNSIGNED_BYTE;
            return GL_RGB;
        case Format::RGBA8_UNORM:
            internalFormat = GL_RGBA8;
            type = GL_UNSIGNED_BYTE;
            return GL_RGBA;
        case Format::RGBA8_SRGB:
            internalFormat = GL_SRGB8_ALPHA8;
            type = GL_UNSIGNED_BYTE;
            return GL_RGBA;
        case Format::RGBA16_FLOAT:
            internalFormat = GL_RGBA16F;
            type = GL_HALF_FLOAT;
            return GL_RGBA;
        case Format::RGBA32_FLOAT:
            internalFormat = GL_RGBA32F;
            type = GL_FLOAT;
            return GL_RGBA;
        case Format::Depth16:
            internalFormat = GL_DEPTH_COMPONENT16;
            type = GL_UNSIGNED_SHORT;
            return GL_DEPTH_COMPONENT;
        case Format::Depth24:
            internalFormat = GL_DEPTH_COMPONENT24;
            type = GL_UNSIGNED_INT;
            return GL_DEPTH_COMPONENT;
        case Format::Depth32F:
            internalFormat = GL_DEPTH_COMPONENT32F;
            type = GL_FLOAT;
            return GL_DEPTH_COMPONENT;
        case Format::Depth24_Stencil8:
            internalFormat = GL_DEPTH24_STENCIL8;
            type = GL_UNSIGNED_INT_24_8;
            return GL_DEPTH_STENCIL;
        default:
            internalFormat = GL_RGBA8;
            type = GL_UNSIGNED_BYTE;
            return GL_RGBA;
    }
}

GLenum GHI_OpenGLBackend::convertFilter(Filter filter) {
    switch (filter) {
        case Filter::Nearest: return GL_NEAREST;
        case Filter::Linear: return GL_LINEAR;
        case Filter::Nearest_Mipmap_Nearest: return GL_NEAREST_MIPMAP_NEAREST;
        case Filter::Linear_Mipmap_Nearest: return GL_LINEAR_MIPMAP_NEAREST;
        case Filter::Nearest_Mipmap_Linear: return GL_NEAREST_MIPMAP_LINEAR;
        case Filter::Linear_Mipmap_Linear: return GL_LINEAR_MIPMAP_LINEAR;
        default: return GL_LINEAR;
    }
}

GLenum GHI_OpenGLBackend::convertWrap(WrapMode mode) {
    switch (mode) {
        case WrapMode::Repeat: return GL_REPEAT;
        case WrapMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case WrapMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
        case WrapMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT;
    }
}

GLenum GHI_OpenGLBackend::convertCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return GL_NEVER;
        case CompareOp::Less: return GL_LESS;
        case CompareOp::Equal: return GL_EQUAL;
        case CompareOp::LessOrEqual: return GL_LEQUAL;
        case CompareOp::Greater: return GL_GREATER;
        case CompareOp::NotEqual: return GL_NOTEQUAL;
        case CompareOp::GreaterOrEqual: return GL_GEQUAL;
        case CompareOp::Always: return GL_ALWAYS;
        default: return GL_LESS;
    }
}

GLenum GHI_OpenGLBackend::convertBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcColor: return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        default: return GL_ONE;
    }
}

GLenum GHI_OpenGLBackend::convertBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return GL_FUNC_ADD;
        case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min: return GL_MIN;
        case BlendOp::Max: return GL_MAX;
        default: return GL_FUNC_ADD;
    }
}

GLenum GHI_OpenGLBackend::convertCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None: return GL_NONE;
        case CullMode::Front: return GL_FRONT;
        case CullMode::Back: return GL_BACK;
        case CullMode::FrontAndBack: return GL_FRONT_AND_BACK;
        default: return GL_BACK;
    }
}

GLenum GHI_OpenGLBackend::convertFrontFace(FrontFace face) {
    switch (face) {
        case FrontFace::Clockwise: return GL_CW;
        case FrontFace::CounterClockwise: return GL_CCW;
        default: return GL_CCW;
    }
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter

