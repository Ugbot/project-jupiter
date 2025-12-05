#pragma once

#include "rendering.h"
#include "camera.h"
#include "lighting.h"
#include "material_system.h"
#include "scene_manager.h"
#include "render_globals.h"
#include "ibl_resources.h"
#include "resources_shadow.h"
#include "pipeline_shadow.h"
#include "application_features.h"
#include "render_features.h"
#include "pbr_push_constants.h"
#include "ecs_bridge.h"
#include "platform/platform.h"
#include "platform/sdl_wrapper.h"
#include "assets/asset_database.h"
#include "assets/gltf_loader.h"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include <unordered_map>

namespace jupiter {
namespace rendering {

// Forward declarations
class VulkanMesh;

namespace vulkan {
    class VulkanRenderer;
    class VulkanBuffer;
    class VulkanPipeline;
}

/**
 * @brief Base application class for easy Vulkan application development
 *
 * Users extend this class and override lifecycle methods:
 * - onInit(): Setup resources (create meshes, load shaders, etc.)
 * - onUpdate(deltaTime): Update game logic
 * - onRender(): Issue rendering commands
 * - onShutdown(): Cleanup resources
 *
 * The Application class manages the window, Vulkan initialization, and main loop.
 *
 * Example usage:
 * @code
 * class MyApp : public Application {
 * public:
 *     MyApp() : Application("My App", 800, 600) {}
 *
 *     void onInit() override {
 *         // Create mesh, load shaders
 *     }
 *
 *     void onRender() override {
 *         // Issue draw commands
 *     }
 * };
 *
 * int main() {
 *     MyApp app;
 *     return app.run();
 * }
 * @endcode
 */
class RENDERING_API Application {
public:
    /**
     * @brief Construct an application
     * @param title Window title
     * @param width Window width
     * @param height Window height
     * @param enableValidation Enable Vulkan validation layers (useful for debugging)
     */
    Application(const std::string& title, uint32_t width, uint32_t height,
                bool enableValidation = false);

    virtual ~Application();

    // No copy, allow move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = default;
    Application& operator=(Application&&) = default;

    /**
     * @brief Run the application
     *
     * This method:
     * 1. Initializes the window and Vulkan
     * 2. Calls onInit()
     * 3. Runs the main loop (onUpdate + onRender)
     * 4. Calls onShutdown()
     * 5. Cleans up resources
     *
     * @return Exit code (0 for success, non-zero for failure)
     */
    int run();

protected:
    // ========================================================================
    // Lifecycle Methods - Override these in your application
    // ========================================================================

    /**
     * @brief Called once after initialization, before main loop
     *
     * Override this to create meshes, load shaders, setup scene, etc.
     * Use helper methods like createVertexBuffer, createIndexBuffer, loadShaders.
     */
    virtual void onInit() {}

    /**
     * @brief Called every frame for game logic updates
     * @param deltaTime Time since last frame in seconds
     *
     * Override this for animation, physics, input handling, etc.
     */
    virtual void onUpdate(float deltaTime) {}

    /**
     * @brief Called every frame for rendering
     *
     * Override this to issue draw commands using helper methods like:
     * - bindPipeline()
     * - drawIndexed()
     *
     * NOTE: If using PBR mode with loadModel(), onRender becomes optional.
     * The scene will be rendered automatically if enableAutoRender() is called.
     */
    virtual void onRender() {}  // Optional if using auto-render

    /**
     * @brief Called once before shutdown
     *
     * Override this to cleanup custom resources.
     * Window and Vulkan resources are cleaned up automatically.
     */
    virtual void onShutdown() {}

    /**
     * @brief Called before the main render pass begins
     * @param cmd Current command buffer
     * @param frameIndex Current frame index
     *
     * Override for pre-render passes (shadow maps, G-buffer, etc.)
     * These render to off-screen targets before the main swapchain pass.
     */
    virtual void onPreRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) {}

    /**
     * @brief Called after the main render pass ends
     * @param cmd Current command buffer
     * @param frameIndex Current frame index  
     *
     * Override for post-process passes (tonemapping, bloom, etc.)
     * Can render to swapchain or intermediate targets.
     */
    virtual void onPostRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) {}

    /**
     * @brief Called to process input events each frame
     * @param deltaTime Time since last frame
     *
     * Override to handle input events (keyboard, mouse, etc.)
     * This is called after window events are polled but before onUpdate.
     * Use SDL_PollEvent() in your override to get input events.
     */
    virtual void onInput(float deltaTime) {}

    // ========================================================================
    // Helper Methods - Use these in your onInit() and onRender()
    // ========================================================================

    /**
     * @brief Vertex structure for simple colored geometry
     */
    struct Vertex {
        float pos[2];    // Position (x, y)
        float color[3];  // Color (r, g, b)
    };

    /**
     * @brief Create a vertex buffer from vertex data
     * @param vertices Pointer to vertex data
     * @param vertexCount Number of vertices
     * @return Pointer to created buffer (managed internally, don't delete)
     */
    vulkan::VulkanBuffer* createVertexBuffer(const Vertex* vertices, uint32_t vertexCount);

    /**
     * @brief Create an index buffer from index data
     * @param indices Pointer to index data (uint16_t)
     * @param indexCount Number of indices
     * @return Pointer to created buffer (managed internally, don't delete)
     */
    vulkan::VulkanBuffer* createIndexBuffer(const uint16_t* indices, uint32_t indexCount);

    /**
     * @brief Load and create a graphics pipeline from shader files
     * @param vertShaderFile Vertex shader filename (e.g., "triangle.vert.spv")
     * @param fragShaderFile Fragment shader filename (e.g., "triangle.frag.spv")
     * @return true if successful
     *
     * Shaders are searched in: ./shaders/, ../shaders/, shaders/
     */
    bool loadShaders(const std::string& vertShaderFile, const std::string& fragShaderFile);

    /**
     * @brief Bind the current pipeline for rendering
     *
     * Call this in onRender() before draw commands.
     */
    void bindPipeline();

    /**
     * @brief Draw indexed geometry
     * @param vertexBuffer Vertex buffer created with createVertexBuffer()
     * @param indexBuffer Index buffer created with createIndexBuffer()
     * @param indexCount Number of indices to draw
     *
     * Call this in onRender() after bindPipeline().
     */
    void drawIndexed(vulkan::VulkanBuffer* vertexBuffer,
                     vulkan::VulkanBuffer* indexBuffer,
                     uint32_t indexCount);

    /**
     * @brief Check if window should close (user clicked X)
     * @return true if window should close
     */
    bool shouldClose() const;

    /**
     * @brief Get window width
     */
    uint32_t getWidth() const { return width_; }

    /**
     * @brief Get window height
     */
    uint32_t getHeight() const { return height_; }

    // ========================================================================
    // Camera Management - Use these to create and control cameras
    // ========================================================================

    /**
     * @brief Create a perspective camera
     * @param fovY Field of view in Y (radians)
     * @param aspect Aspect ratio (width/height)
     * @param near Near clipping plane
     * @param far Far clipping plane
     * @return Pointer to created camera
     */
    PerspectiveCamera* createPerspectiveCamera(
        float fovY = math::PI / 4.0f,
        float aspect = 0.0f,  // 0 = auto-calculate from window
        float near = 0.1f,
        float far = 100.0f);

    /**
     * @brief Create an orthographic camera
     * @param left Left edge
     * @param right Right edge
     * @param bottom Bottom edge
     * @param top Top edge
     * @param near Near clipping plane
     * @param far Far clipping plane
     * @return Pointer to created camera
     */
    OrthographicCamera* createOrthographicCamera(
        float left = -10.0f,
        float right = 10.0f,
        float bottom = -10.0f,
        float top = 10.0f,
        float near = 0.1f,
        float far = 100.0f);

    /**
     * @brief Set the active camera for rendering
     * @param camera Pointer to camera (must be created by create*Camera)
     */
    void setActiveCamera(Camera* camera);

    /**
     * @brief Get the active camera
     * @return Pointer to active camera (may be nullptr)
     */
    Camera* getActiveCamera() const { return activeCamera_; }

    // ========================================================================
    // PBR & Model Loading - Use these for 3D models with materials
    // ========================================================================

    /**
     * @brief Enable automatic PBR rendering
     *
     * When enabled, all models loaded with loadModel() are automatically
     * rendered in onRender(). User can still add custom rendering.
     *
     * Call this in onInit() before loading models.
     */
    void enableAutoRender();

    /**
     * @brief Load GLTF/GLB model and add to scene
     *
     * Loads model from file, creates GPU resources, and adds to scene.
     * Returns handles to renderables for manual control if needed.
     *
     * @param filepath Path to .gltf or .glb file
     * @return Vector of renderable handles (one per mesh in model)
     *
     * Example:
     * @code
     * enableAutoRender();
     * loadModel("models/helmet.gltf");  // Automatically rendered
     * @endcode
     */
    std::vector<RenderableHandle> loadModel(const std::string& filepath);

    // ========================================================================
    // Primitive Shape Spawning
    // ========================================================================

    /**
     * @brief Spawn a cube primitive
     * @param position World position (x, y, z)
     * @param size Edge length
     * @param color Base color (r, g, b) - optional, defaults to white
     * @return Handle to the renderable
     */
    RenderableHandle spawnCube(const float position[3], float size = 1.0f, 
                               const float color[3] = nullptr);

    /**
     * @brief Spawn a box primitive
     * @param position World position (x, y, z)
     * @param dimensions Box dimensions (width, height, depth)
     * @param color Base color (r, g, b) - optional
     * @return Handle to the renderable
     */
    RenderableHandle spawnBox(const float position[3], const float dimensions[3],
                              const float color[3] = nullptr);

    /**
     * @brief Spawn a UV sphere primitive
     * @param position World position (x, y, z)
     * @param radius Sphere radius
     * @param segments Horizontal divisions (default 32)
     * @param rings Vertical divisions (default 16)
     * @param color Base color (r, g, b) - optional
     * @return Handle to the renderable
     */
    RenderableHandle spawnSphere(const float position[3], float radius = 1.0f,
                                 uint32_t segments = 32, uint32_t rings = 16,
                                 const float color[3] = nullptr);

    /**
     * @brief Spawn a cylinder primitive
     * @param position World position (x, y, z)
     * @param radius Cylinder radius
     * @param height Cylinder height
     * @param segments Radial segments (default 32)
     * @param color Base color (r, g, b) - optional
     * @return Handle to the renderable
     */
    RenderableHandle spawnCylinder(const float position[3], float radius = 0.5f,
                                   float height = 1.0f, uint32_t segments = 32,
                                   const float color[3] = nullptr);

    /**
     * @brief Spawn a plane/ground primitive
     * @param position World position (x, y, z)
     * @param width X dimension
     * @param depth Z dimension
     * @param color Base color (r, g, b) - optional
     * @return Handle to the renderable
     */
    RenderableHandle spawnPlane(const float position[3], float width = 10.0f,
                                float depth = 10.0f, const float color[3] = nullptr);

    /**
     * @brief Spawn a capsule primitive
     * @param position World position (x, y, z)
     * @param radius Capsule radius
     * @param height Total height
     * @param color Base color (r, g, b) - optional
     * @return Handle to the renderable
     */
    RenderableHandle spawnCapsule(const float position[3], float radius = 0.5f,
                                  float height = 2.0f, const float color[3] = nullptr);

    /**
     * @brief Update the transform of a spawned primitive or model
     * @param handle Handle to the renderable
     * @param position New world position (x, y, z)
     * @param rotation Rotation angles in radians (pitch, yaw, roll) - optional
     * @param scale Uniform scale factor - optional
     */
    void setRenderableTransform(RenderableHandle handle, 
                                const float position[3],
                                const float rotation[3] = nullptr,
                                float scale = 1.0f);

    /**
     * @brief Update renderable transform with a matrix directly
     * @param handle Handle to the renderable
     * @param transform 4x4 transformation matrix (column-major)
     */
    void setRenderableMatrix(RenderableHandle handle, const float* transform);

    /**
     * @brief Add directional light (sun, moon)
     *
     * @param direction Light direction vector (will be normalized)
     * @param color RGB color (0-1 range)
     * @param intensity Light intensity multiplier
     * @return Light index (for updates), or -1 if full
     */
    int32_t addDirectionalLight(const float direction[3], const float color[3], float intensity = 1.0f);

    /**
     * @brief Add point light (bulb, torch)
     *
     * @param position World position
     * @param color RGB color (0-1 range)
     * @param intensity Light intensity multiplier
     * @param radius Maximum light range
     * @return Light index (for updates), or -1 if full
     */
    int32_t addPointLight(const float position[3], const float color[3],
                         float intensity = 1.0f, float radius = 10.0f);

    /**
     * @brief Add spot light (flashlight, car headlight)
     *
     * @param position World position
     * @param direction Light direction (will be normalized)
     * @param color RGB color (0-1 range)
     * @param intensity Light intensity multiplier
     * @param innerAngle Inner cone angle in radians
     * @param outerAngle Outer cone angle in radians
     * @return Light index (for updates), or -1 if full
     */
    int32_t addSpotLight(const float position[3], const float direction[3], const float color[3],
                        float intensity = 1.0f, float innerAngle = 0.436f, float outerAngle = 0.524f);

    /**
     * @brief Set ambient light (global illumination approximation)
     *
     * @param color RGB color (0-1 range)
     * @param intensity Light intensity multiplier
     */
    void setAmbientLight(const float color[3], float intensity = 1.0f);

    /**
     * @brief Enable/disable shadow mapping
     * @param enabled true to enable shadows
     */
    void setShadowsEnabled(bool enabled) { shadowsEnabled_ = enabled; }
    
    /**
     * @brief Check if shadows are enabled
     */
    bool areShadowsEnabled() const { return shadowsEnabled_; }

    // ========================================================================
    // HDR Pipeline and Tonemapping
    // ========================================================================

    /**
     * @brief Enable HDR rendering pipeline
     * 
     * When enabled, the scene is rendered to an HDR framebuffer, then
     * tonemapped to the final swapchain image. This allows for:
     * - Proper HDR lighting with values > 1.0
     * - Configurable tonemapping (ACES, Reinhard, etc.)
     * - Auto-exposure based on scene luminance
     * 
     * @param enable true to enable HDR pipeline
     */
    void enableHDR(bool enable);

    /**
     * @brief Check if HDR pipeline is enabled
     */
    bool isHDREnabled() const { return useHDRPipeline_; }

    /**
     * @brief Set manual exposure value
     * 
     * Only used when auto-exposure is disabled.
     * 
     * @param exposure Exposure multiplier (default 1.0)
     */
    void setExposure(float exposure);

    /**
     * @brief Get current exposure value
     */
    float getExposure() const { return manualExposure_; }

    /**
     * @brief Enable/disable auto-exposure
     * 
     * When enabled, exposure is automatically adjusted based on
     * average scene luminance (eye adaptation effect).
     * 
     * @param enable true to enable auto-exposure
     */
    void setAutoExposure(bool enable);

    /**
     * @brief Check if auto-exposure is enabled
     */
    bool isAutoExposureEnabled() const { return autoExposureEnabled_; }

    /**
     * @brief Set tonemapping operator
     * @param op Tonemapping operator to use
     */
    void setTonemapOperator(TonemapOperator op);

    /**
     * @brief Get current tonemapping operator
     */
    TonemapOperator getTonemapOperator() const { return tonemapOperator_; }

    /**
     * @brief Get application features (for advanced feature control)
     */
    ApplicationFeatures* getAppFeatures() { return appFeatures_.get(); }

    /**
     * @brief Get render features configuration
     */
    RenderFeatures& getRenderFeatures();

    // ========================================================================
    // PBR Push Constants - Runtime Tuning
    // ========================================================================

    /**
     * @brief Set PBR push constants for runtime tuning
     * 
     * These values are applied to the PBR shader every frame.
     * 
     * @param params PBR push constants
     */
    void setPBRParams(const PBRPushConstants& params);

    /**
     * @brief Get current PBR push constants
     */
    const PBRPushConstants& getPBRParams() const { return pbrPushConstants_; }

    /**
     * @brief Get mutable reference to PBR push constants for modification
     */
    PBRPushConstants& getPBRParamsMutable() { return pbrPushConstants_; }

    /**
     * @brief Set direct light intensity multiplier
     * @param intensity Value to multiply all direct lights by
     */
    void setDirectLightIntensity(float intensity);

    /**
     * @brief Set ambient/IBL intensity multiplier
     * @param intensity Value to multiply ambient contribution by
     */
    void setAmbientIntensity(float intensity);

    /**
     * @brief Set shadow intensity (0 = no shadows, 1 = full shadows)
     * @param intensity Shadow darkness
     */
    void setShadowIntensity(float intensity);

    /**
     * @brief Set PBR debug visualization mode
     * @param mode Debug mode flag (use FLAG_DEBUG_* constants)
     */
    void setPBRDebugMode(uint32_t mode);

    /**
     * @brief Clear PBR debug visualization
     */
    void clearPBRDebugMode();

    /**
     * @brief Get render globals (for advanced usage)
     *
     * Direct access to render globals for custom rendering.
     */
    RenderGlobals* getRenderGlobals() { return renderGlobals_.get(); }

    /**
     * @brief Get scene manager (for advanced usage)
     *
     * Direct access to scene manager for manual renderable control.
     */
    SceneManager* getSceneManager() { return sceneManager_.get(); }

    /**
     * @brief Get light manager (for advanced usage)
     *
     * Direct access to light manager for manual light control.
     */
    LightManager* getLightManager() { return lightManager_.get(); }

    /**
     * @brief Request the application to close
     */
    void requestClose();

    // ========================================================================
    // ECS Integration - For data-driven rendering
    // ========================================================================

    /**
     * @brief Enable ECS-driven rendering
     *
     * When enabled, the renderer pulls visible instances from the ECS World
     * automatically each frame. This provides:
     * - Lock-free snapshot acquisition (render never waits for simulation)
     * - Automatic GPU upload of instance data
     * - Material-sorted batching for efficiency
     *
     * Call this in onInit() before using ECS rendering.
     *
     * @param world Pointer to ECS world (must outlive Application)
     * @param maxInstances Maximum instances to render (default 65536)
     * @return true if successful
     */
    bool enableECSRendering(ecs::World* world, uint32_t maxInstances = MAX_RENDER_INSTANCES);

    /**
     * @brief Check if ECS rendering is enabled
     */
    bool isECSRenderingEnabled() const { return ecsWorld_ != nullptr; }

    /**
     * @brief Get the ECS renderer bridge
     * @return Pointer to ECS renderer (nullptr if not enabled)
     */
    ECSRenderer* getECSRenderer() { return ecsRenderer_.get(); }

    /**
     * @brief Get the ECS world being rendered
     * @return Pointer to ECS world (nullptr if not enabled)
     */
    ecs::World* getECSWorld() { return ecsWorld_; }

protected:
    // ========================================================================
    // Protected Accessors - For derived classes needing custom rendering
    // ========================================================================

    /**
     * @brief Get the Vulkan renderer for custom pipeline creation
     */
    vulkan::VulkanRenderer* getRenderer() { return renderer_.get(); }

    /**
     * @brief Get current frame index (for per-frame resources)
     */
    uint32_t getCurrentFrameIndex() const { return currentFrameIndex_; }

private:
    std::string title_;
    uint32_t width_;
    uint32_t height_;
    bool enableValidation_;

    // SDL wrapper (must be initialized before window)
    std::unique_ptr<platform::sdl::SDLWrapper> sdl_;

    // Window and renderer (managed internally)
    std::unique_ptr<Window> window_;
    std::unique_ptr<vulkan::VulkanRenderer> renderer_;

    // Buffers created by user (managed internally for cleanup)
    std::vector<std::unique_ptr<vulkan::VulkanBuffer>> buffers_;

    // Cameras created by user (managed internally for cleanup)
    std::vector<std::unique_ptr<Camera>> cameras_;
    Camera* activeCamera_ = nullptr;

    // PBR systems (initialized when enableAutoRender() is called)
    std::unique_ptr<assets::AssetDatabase> assetDatabase_;
    std::unique_ptr<assets::GLTFLoader> gltfLoader_;
    std::unique_ptr<MaterialSystem> materialSystem_;
    std::unique_ptr<SceneManager> sceneManager_;
    std::unique_ptr<RenderGlobals> renderGlobals_;
    std::unique_ptr<LightManager> lightManager_;
    std::unique_ptr<IBLResources> iblResources_;
    std::unique_ptr<ResourcesShadow> shadowResources_;
    std::unique_ptr<PipelineShadow> shadowPipeline_;
    bool shadowsEnabled_ = true;

    // HDR pipeline and tonemapping
    std::unique_ptr<ApplicationFeatures> appFeatures_;
    bool useHDRPipeline_ = false;
    float manualExposure_ = 1.0f;
    bool autoExposureEnabled_ = false;
    TonemapOperator tonemapOperator_ = TonemapOperator::ACES;

    // PBR push constants for runtime tuning
    PBRPushConstants pbrPushConstants_;

    // Loaded textures (image UUID -> VulkanTexture)
    std::unordered_map<std::string, std::unique_ptr<VulkanTexture>> textures_;

    // Primitive meshes (owned by Application for lifetime management)
    std::vector<std::unique_ptr<VulkanMesh>> primitiveMeshes_;

    // PBR pipeline (created when enableAutoRender() is called)
    std::unique_ptr<vulkan::VulkanPipeline> pbrPipeline_;
    bool autoRenderEnabled_ = false;
    uint32_t currentFrameIndex_ = 0;

    // ECS integration (optional, for data-driven rendering)
    std::unique_ptr<ECSRenderer> ecsRenderer_;
    ecs::World* ecsWorld_ = nullptr;

    // Timing
    platform::Timer timer_;

    bool initialize();
    void shutdown();
    void mainLoop();
    double getCurrentTime() const;
    void updateCameraUniforms();

    // PBR initialization helpers
    bool initializePBRSystems();
    bool createPBRPipeline();
    void renderPBRScene(uint32_t frameIndex);

    // Shadow mapping helpers
    bool initializeShadowMapping();
    void renderShadowPass(uint32_t frameIndex);
    void updateShadowMatrices(uint32_t frameIndex);

    // HDR pipeline helpers
    bool initializeHDRPipeline();
    void renderWithHDR(uint32_t frameIndex);
    void renderTonemapPass(VkCommandBuffer cmd, uint32_t frameIndex);
};

} // namespace rendering
} // namespace jupiter
