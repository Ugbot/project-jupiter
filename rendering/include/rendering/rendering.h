#pragma once

#ifdef _WIN32
    #ifdef RENDERING_EXPORTS
        #define RENDERING_API __declspec(dllexport)
    #elif defined(RENDERING_IMPORTS)
        #define RENDERING_API __declspec(dllimport)
    #else
        #define RENDERING_API
    #endif
#else
    #define RENDERING_API
#endif

#include <memory>
#include <string>
#include <vector>
#include <functional>

// Include math types for rendering
#include "../../math/include/math/math.h"

// GLFW for windowing
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace jupiter {
namespace platform {
    class Window;
}

namespace rendering {

// Forward declarations
class Mesh;
class Texture;
class Pipeline;
class Shader;
class RenderContext;

/**
 * @brief Window configuration
 */
struct RENDERING_API WindowConfig {
    std::string title = "Jupiter Engine";
    uint32_t width = 800;
    uint32_t height = 600;
    bool resizable = false;
    bool visible = true;
};

/**
 * @brief Window class for cross-platform window management
 *
 * This class wraps the platform::Window interface and provides
 * Vulkan-specific functionality for surface creation.
 */
class RENDERING_API Window {
public:
    Window();
    ~Window();

    bool initialize(const WindowConfig& config);
    void shutdown();

    bool shouldClose() const;
    void pollEvents();
    void requestClose();

    uint32_t getWidth() const;
    uint32_t getHeight() const;

    // Vulkan surface creation
    VkResult createVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) const;

    // Get required Vulkan extensions
    std::vector<const char*> getRequiredExtensions() const;

    // Get the underlying platform window
    platform::Window* getPlatformWindow() const { return m_window.get(); }

private:
    std::unique_ptr<platform::Window> m_window;
    WindowConfig m_config;
};

/**
 * @brief Rendering capabilities and limits
 */
struct RENDERING_API RenderCapabilities {
    bool supportsVulkan = false;
    bool supportsRaytracing = false;
    bool supportsTessellation = false;
    bool supportsGeometryShaders = false;
    uint32_t maxTextureSize = 0;
    uint32_t maxVertexAttributes = 0;
    uint32_t maxColorAttachments = 0;
    std::string deviceName;
    std::string apiVersion;
};

/**
 * @brief Render pass configuration
 */
struct RENDERING_API RenderPassConfig {
    bool clearColor = true;
    bool clearDepth = true;
    bool clearStencil = false;
    math::Vector4 clearColorValue = {0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepthValue = 1.0f;
    uint32_t clearStencilValue = 0;
    bool enableDepthTest = true;
    bool enableBlending = false;
};

/**
 * @brief Rendering statistics
 */
struct RENDERING_API RenderStats {
    uint32_t drawCalls = 0;
    uint32_t trianglesDrawn = 0;
    uint32_t texturesBound = 0;
    uint32_t shadersBound = 0;
    double frameTimeMs = 0.0;
    uint32_t fps = 0;
};

/**
 * @brief High-level rendering interface
 *
 * Provides abstracted rendering operations that hide Vulkan complexity.
 * Users can render meshes, bind textures, set pipelines without knowing Vulkan.
 */
class RENDERING_API Renderer {
public:
    /**
     * @brief Get singleton renderer instance
     */
    static Renderer& instance();

    /**
     * @brief Initialize the renderer with window configuration
     * @param windowConfig Configuration for the rendering window
     * @param enableValidation Enable Vulkan validation layers (debug only)
     * @return true if initialization successful
     */
    bool initialize(const WindowConfig& windowConfig, bool enableValidation = false);

    /**
     * @brief Shutdown the renderer and free resources
     */
    void shutdown();

    /**
     * @brief Check if renderer is initialized and ready
     */
    bool isReady() const;

    /**
     * @brief Begin a new frame
     * @return true if frame can be rendered
     */
    bool beginFrame();

    /**
     * @brief End the current frame and present to screen
     */
    void endFrame();

    /**
     * @brief Set the current render pass configuration
     */
    void setRenderPass(const RenderPassConfig& config);

    /**
     * @brief Set the current rendering pipeline
     */
    void setPipeline(Pipeline* pipeline);

    /**
     * @brief Bind a texture to a slot
     */
    void bindTexture(Texture* texture, uint32_t slot = 0);

    /**
     * @brief Set shader uniform (push constant)
     */
    void setUniform(const std::string& name, const void* data, size_t size);

    /**
     * @brief Draw a mesh with optional transform
     */
    void drawMesh(Mesh* mesh, const math::Matrix4x4* transform = nullptr);

    /**
     * @brief Draw multiple instances of a mesh
     */
    void drawMeshInstanced(Mesh* mesh, uint32_t instanceCount,
                          const math::Matrix4x4* transforms = nullptr);

    /**
     * @brief Draw fullscreen quad (for post-processing)
     */
    void drawFullscreenQuad();

    /**
     * @brief Set viewport
     */
    void setViewport(float x, float y, float width, float height);

    /**
     * @brief Set scissor rectangle
     */
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    /**
     * @brief Get current rendering capabilities
     */
    const RenderCapabilities& getCapabilities() const;

    /**
     * @brief Get current frame rendering statistics
     */
    const RenderStats& getStats() const;

    /**
     * @brief Wait for all rendering operations to complete
     */
    void waitIdle();

    /**
     * @brief Create a mesh resource
     */
    std::unique_ptr<Mesh> createMesh();

    /**
     * @brief Create a texture resource
     */
    std::unique_ptr<Texture> createTexture();

    /**
     * @brief Create a shader resource
     */
    std::unique_ptr<Shader> createShader();

    /**
     * @brief Create a pipeline resource
     */
    std::unique_ptr<Pipeline> createPipeline();

private:
    Renderer() = default;
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Internal Vulkan state (hidden from users)
    class VulkanRendererImpl;
    std::unique_ptr<VulkanRendererImpl> m_impl;

    RenderCapabilities m_capabilities;
    RenderStats m_stats;
};

/**
 * @brief Mesh resource for rendering 3D geometry
 */
class RENDERING_API Mesh {
public:
    virtual ~Mesh() = default;

    /**
     * @brief Set vertex data
     * @param vertices Pointer to vertex data
     * @param vertexCount Number of vertices
     * @param vertexStride Size of each vertex in bytes
     */
    virtual void setVertices(const void* vertices, uint32_t vertexCount, uint32_t vertexStride) = 0;

    /**
     * @brief Set index data
     * @param indices Pointer to index data
     * @param indexCount Number of indices
     * @param indexType Type of indices (16-bit or 32-bit)
     */
    virtual void setIndices(const void* indices, uint32_t indexCount, bool use32BitIndices = false) = 0;

    /**
     * @brief Set vertex attribute layout
     */
    virtual void setVertexLayout(const std::vector<uint32_t>& attributeSizes) = 0;

    /**
     * @brief Upload mesh data to GPU
     */
    virtual void upload() = 0;

    /**
     * @brief Get vertex count
     */
    virtual uint32_t getVertexCount() const = 0;

    /**
     * @brief Get index count
     */
    virtual uint32_t getIndexCount() const = 0;

    /**
     * @brief Check if mesh uses indexed rendering
     */
    virtual bool isIndexed() const = 0;
};

/**
 * @brief Texture resource for rendering
 */
class RENDERING_API Texture {
public:
    enum class Format {
        RGBA8,
        RGB8,
        R8,
        DEPTH24_STENCIL8,
        DEPTH32
    };

    enum class Filter {
        NEAREST,
        LINEAR
    };

    enum class Wrap {
        REPEAT,
        CLAMP_TO_EDGE,
        CLAMP_TO_BORDER
    };

    virtual ~Texture() = default;

    /**
     * @brief Create texture from pixel data
     */
    virtual void create(uint32_t width, uint32_t height, Format format, const void* data = nullptr) = 0;

    /**
     * @brief Load texture from file
     */
    virtual bool loadFromFile(const std::string& path) = 0;

    /**
     * @brief Set texture filtering
     */
    virtual void setFilter(Filter minFilter, Filter magFilter) = 0;

    /**
     * @brief Set texture wrapping
     */
    virtual void setWrap(Wrap wrapS, Wrap wrapT) = 0;

    /**
     * @brief Generate mipmaps
     */
    virtual void generateMipmaps() = 0;

    /**
     * @brief Get texture dimensions
     */
    virtual math::Vector2 getSize() const = 0;

    /**
     * @brief Get texture format
     */
    virtual Format getFormat() const = 0;
};

/**
 * @brief Shader resource
 */
class RENDERING_API Shader {
public:
    enum class Type {
        VERTEX,
        FRAGMENT,
        GEOMETRY,
        COMPUTE,
        TESSELLATION_CONTROL,
        TESSELLATION_EVALUATION
    };

    virtual ~Shader() = default;

    /**
     * @brief Load shader from SPIR-V bytecode
     */
    virtual bool loadFromSPIRV(Type type, const std::vector<uint32_t>& bytecode) = 0;

    /**
     * @brief Load shader from GLSL source
     */
    virtual bool loadFromGLSL(Type type, const std::string& source) = 0;

    /**
     * @brief Load shader from file
     */
    virtual bool loadFromFile(Type type, const std::string& path) = 0;

    /**
     * @brief Compile shader (if needed)
     */
    virtual bool compile() = 0;

    /**
     * @brief Get shader type
     */
    virtual Type getType() const = 0;
};

/**
 * @brief Rendering pipeline configuration
 */
class RENDERING_API Pipeline {
public:
    struct Config {
        Shader* vertexShader = nullptr;
        Shader* fragmentShader = nullptr;
        Shader* geometryShader = nullptr;
        Shader* tessControlShader = nullptr;
        Shader* tessEvalShader = nullptr;

        bool enableDepthTest = true;
        bool enableBlending = false;
        bool wireframe = false;

        std::vector<uint32_t> vertexLayout; // Attribute sizes in bytes

        // Blend mode
        enum class BlendFactor {
            ZERO, ONE,
            SRC_COLOR, ONE_MINUS_SRC_COLOR,
            DST_COLOR, ONE_MINUS_DST_COLOR,
            SRC_ALPHA, ONE_MINUS_SRC_ALPHA,
            DST_ALPHA, ONE_MINUS_DST_ALPHA
        };

        BlendFactor srcBlend = BlendFactor::SRC_ALPHA;
        BlendFactor dstBlend = BlendFactor::ONE_MINUS_SRC_ALPHA;
    };

    virtual ~Pipeline() = default;

    /**
     * @brief Configure the pipeline
     */
    virtual void configure(const Config& config) = 0;

    /**
     * @brief Rebuild the pipeline (call after configuration changes)
     */
    virtual bool rebuild() = 0;

    /**
     * @brief Check if pipeline is valid
     */
    virtual bool isValid() const = 0;
};

/**
 * @brief Initialize the rendering subsystem
 */
RENDERING_API bool initialize();

/**
 * @brief Shutdown the rendering subsystem
 */
RENDERING_API void shutdown();

} // namespace rendering
} // namespace jupiter

// Include the Application class for easy application development
#include "rendering/application.h"