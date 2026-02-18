#pragma once

/**
 * @file ghi_types.h
 * @brief Graphics Hardware Interface - Type definitions
 * 
 * Core types, enums, and handles for the GHI abstraction layer.
 * Based on Venus GHI patterns, adapted for C++ and modern GPUs.
 */

#include <cstdint>
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Backend Selection
// ============================================================================

/**
 * @brief Graphics backend type
 */
enum class Backend {
    Vulkan,    // Primary for Linux/Windows
    Metal,     // Primary for macOS/iOS
    OpenGL,    // Universal fallback
    DX12       // Future: Windows native
};

// ============================================================================
// Resource Handles (Type-Safe)
// ============================================================================

struct BufferHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct TextureHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct ShaderHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct PipelineHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct RenderPassHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct RenderTargetHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct FramebufferHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct SamplerHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

// ============================================================================
// Enumerations
// ============================================================================

enum class BufferType {
    Vertex,
    Index,
    Uniform,
    Storage,
    Indirect
};

enum class BufferUsage {
    Static,     // Written once, read many (vertex/index data)
    Dynamic,    // Updated frequently (uniforms)
    Stream      // Written once per frame (streaming data)
};

enum class TextureType {
    Texture2D,
    TextureCube,
    Texture3D,
    TextureArray
};

enum class Format {
    // 8-bit formats
    R8_UNORM,
    RG8_UNORM,
    RGB8_UNORM,
    RGBA8_UNORM,
    RGB8_SRGB,
    RGBA8_SRGB,
    
    // 16-bit formats
    R16_FLOAT,
    RG16_FLOAT,
    RGB16_FLOAT,
    RGBA16_FLOAT,
    
    // 32-bit formats
    R32_FLOAT,
    RG32_FLOAT,
    RGB32_FLOAT,
    RGBA32_FLOAT,
    
    // Depth/stencil
    Depth16,
    Depth24,
    Depth32F,
    Depth24_Stencil8,
    Depth32F_Stencil8,
    
    // Compressed (future)
    BC1_RGB,
    BC3_RGBA,
    BC7_RGBA
};

enum class Filter {
    Nearest,
    Linear,
    Nearest_Mipmap_Nearest,
    Linear_Mipmap_Nearest,
    Nearest_Mipmap_Linear,
    Linear_Mipmap_Linear
};

enum class WrapMode {
    Repeat,
    ClampToEdge,
    ClampToBorder,
    MirroredRepeat
};

/**
 * @brief Texture usage flags (can be combined with |)
 */
enum class TextureUsage : uint32_t {
    Sampled     = 0x01,  // Can be sampled in shaders
    Storage     = 0x02,  // Can be read/written in compute shaders
    RenderTarget = 0x04, // Can be used as render target
    DepthStencil = 0x08, // Can be used as depth/stencil attachment
    TransferSrc  = 0x10, // Can be source of copy operations
    TransferDst  = 0x20  // Can be destination of copy operations
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasUsage(TextureUsage flags, TextureUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

enum class PrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList
};

enum class CullMode {
    None,
    Front,
    Back,
    FrontAndBack
};

enum class FrontFace {
    Clockwise,
    CounterClockwise
};

enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha
};

enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

// ============================================================================
// Structures
// ============================================================================

struct BufferCreateInfo {
    BufferType type = BufferType::Vertex;
    BufferUsage usage = BufferUsage::Static;
    size_t size = 0;
    const void* data = nullptr;  // Initial data (optional)
};

struct TextureCreateInfo {
    TextureType type = TextureType::Texture2D;
    Format format = Format::RGBA8_UNORM;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;       // For 3D textures or array layers
    uint32_t mipLevels = 1;   // 0 = auto-generate
    TextureUsage usage = TextureUsage::Sampled;  // How texture will be used
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    WrapMode wrapS = WrapMode::Repeat;
    WrapMode wrapT = WrapMode::Repeat;
    WrapMode wrapR = WrapMode::Repeat;
    const void* data = nullptr;  // Initial data (optional)
};

struct ShaderSource {
    const char* vertexSource = nullptr;
    const char* fragmentSource = nullptr;
    const char* geometrySource = nullptr;  // Optional
    const char* computeSource = nullptr;   // Optional
    
    // Or file paths
    const char* vertexPath = nullptr;
    const char* fragmentPath = nullptr;
    const char* computePath = nullptr;
};

/**
 * @brief Sampler creation info
 * 
 * Describes texture sampling behavior (filtering, wrapping, etc.)
 * Samplers can be shared across multiple textures.
 */
struct SamplerCreateInfo {
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear_Mipmap_Linear;
    WrapMode wrapS = WrapMode::Repeat;
    WrapMode wrapT = WrapMode::Repeat;
    WrapMode wrapR = WrapMode::Repeat;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    float mipLodBias = 0.0f;
    bool anisotropyEnabled = true;
    float maxAnisotropy = 16.0f;
    bool compareEnabled = false;           // For shadow maps
    CompareOp compareOp = CompareOp::Less; // For shadow maps
    glm::vec4 borderColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // For ClampToBorder
    const char* debugName = nullptr;
};

/**
 * @brief Attachment description for render targets
 */
struct RenderTargetAttachment {
    Format format = Format::RGBA8_UNORM;
    bool clear = true;
    glm::vec4 clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    float clearDepth = 1.0f;
    uint32_t clearStencil = 0;
};

/**
 * @brief Render target creation info
 * 
 * Describes an off-screen render target (framebuffer) for:
 * - Deferred rendering G-Buffer
 * - Shadow maps
 * - Post-processing effects
 * - Reflection/refraction captures
 */
struct RenderTargetCreateInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    
    // Color attachments (up to maxColorAttachments)
    std::vector<RenderTargetAttachment> colorAttachments;
    
    // Depth/stencil attachment (optional)
    bool hasDepth = true;
    Format depthFormat = Format::Depth32F;
    bool clearDepth = true;
    float depthClearValue = 1.0f;
    
    // Multisampling
    uint32_t sampleCount = 1;  // 1 = no MSAA
    
    // Debug name
    const char* debugName = nullptr;
};

struct RenderState {
    ShaderHandle shader;  // Active shader pipeline
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    bool blendEnabled = false;

    // Blend state
    BlendFactor srcColorBlendFactor = BlendFactor::SrcAlpha;
    BlendFactor dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
    BlendOp alphaBlendOp = BlendOp::Add;

    bool cullFaceEnabled = true;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    CompareOp depthCompareOp = CompareOp::Less;
    glm::vec4 clearColor = glm::vec4(0.5f, 0.7f, 0.9f, 1.0f);  // Sky blue
    float clearDepth = 1.0f;
    uint32_t clearStencil = 0;
};

struct DrawCommand {
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstInstance = 0;
};

// ============================================================================
// Backend Capabilities
// ============================================================================

struct Capabilities {
    // Tier 1 - Required (all backends)
    bool hasIndexedDraw = true;
    bool hasDepthTest = true;
    bool hasMRT = true;
    uint32_t maxColorAttachments = 4;
    uint32_t maxTextureSize = 4096;
    
    // Tier 2 - Near-common (Vulkan/Metal/DX12, optional OpenGL)
    bool hasComputeShaders = false;
    bool hasIndirectDraw = false;
    bool hasStorageBuffers = false;
    uint32_t maxComputeWorkGroupSize[3] = {0, 0, 0};
    
    // Tier 3 - Optimization features
    bool hasSubgroups = false;
    bool hasTessellation = false;
    bool hasGeometryShaders = false;
    uint32_t subgroupSize = 0;
    
    // Tier 4 - Backend-specific
    bool hasRayTracing = false;
    bool hasMeshShaders = false;
    bool hasTileShaders = false;           // Metal-only
    bool hasMemorylessTextures = false;    // Metal-only
    bool hasArgumentBuffers = false;       // Metal-only
    
    // Backend info
    Backend backend = Backend::Vulkan;
    std::string deviceName = "";
    std::string driverVersion = "";
};

} // namespace ghi
} // namespace rendering
} // namespace jupiter

