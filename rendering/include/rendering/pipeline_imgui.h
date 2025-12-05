#pragma once

/**
 * @file pipeline_imgui.h
 * @brief ImGui integration pipeline for Jupiter engine
 *
 * Provides immediate mode GUI rendering using Dear ImGui with SDL3+Vulkan backend.
 * Supports debug overlays, performance stats, and optional ImGuizmo for scene editing.
 */

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <functional>
#include <string>

// Forward declarations
struct SDL_Window;

namespace jupiter::rendering {

// Forward declarations
class Camera;
class SceneManager;

/**
 * @brief Gizmo manipulation modes
 */
enum class GizmoMode {
    None = 0,
    Translate = 1,
    Rotate = 2,
    Scale = 3
};

/**
 * @brief Frame timing data for UI display
 */
struct FrameStats {
    float currentFPS = 0.0f;
    float deltaTimeMs = 0.0f;
    float avgFrameTimeMs = 0.0f;
    float minFrameTimeMs = 0.0f;
    float maxFrameTimeMs = 0.0f;

    // Ring buffer for FPS graph
    static constexpr size_t GRAPH_SIZE = 120;
    float fpsHistory[GRAPH_SIZE] = {};
    size_t historyIndex = 0;

    void update(float deltaTime);
    const float* getGraph() const { return fpsHistory; }
    size_t getGraphSize() const { return GRAPH_SIZE; }
};

/**
 * @brief UI callback for custom ImGui content
 */
using ImGuiCallback = std::function<void()>;

/**
 * @brief ImGui rendering pipeline with SDL3+Vulkan backend
 */
class PipelineImGui {
public:
    PipelineImGui() = default;
    ~PipelineImGui();

    // Non-copyable
    PipelineImGui(const PipelineImGui&) = delete;
    PipelineImGui& operator=(const PipelineImGui&) = delete;

    /**
     * @brief Initialize ImGui with Vulkan context
     * @param device Vulkan logical device
     * @param physicalDevice Vulkan physical device
     * @param instance Vulkan instance
     * @param graphicsQueue Graphics queue for rendering
     * @param graphicsQueueFamily Queue family index
     * @param window SDL window handle
     * @param renderPass Render pass for ImGui rendering
     * @param swapchainFormat Swapchain image format
     * @param imageCount Number of swapchain images
     * @return true on success
     */
    bool initialize(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkInstance instance,
                    VkQueue graphicsQueue,
                    uint32_t graphicsQueueFamily,
                    SDL_Window* window,
                    VkRenderPass renderPass,
                    VkFormat swapchainFormat,
                    uint32_t imageCount);

    void destroy();

    /**
     * @brief Begin a new ImGui frame
     * Call this at the start of your frame, before any ImGui calls
     */
    void beginFrame();

    /**
     * @brief End ImGui frame and prepare for rendering
     * Call this after all ImGui calls, before fillCommandBuffer
     */
    void endFrame();

    /**
     * @brief Record ImGui draw commands to command buffer
     * @param cmd Command buffer to record to
     * @param framebuffer Framebuffer to render to
     * @param renderArea Render area extent
     */
    void fillCommandBuffer(VkCommandBuffer cmd,
                          VkFramebuffer framebuffer,
                          VkExtent2D renderArea);

    // =========================================================================
    // Built-in UI Windows
    // =========================================================================

    /**
     * @brief Show performance statistics window
     */
    void showFrameStats(FrameStats& stats);

    /**
     * @brief Show renderer settings window
     */
    void showRendererSettings(float& exposure, float& gamma,
                              bool& ssaoEnabled, bool& shadowsEnabled);

    /**
     * @brief Show light editor window
     */
    void showLightEditor(glm::vec3& lightDir, glm::vec3& lightColor,
                        float& lightIntensity);

    /**
     * @brief Show camera info window
     */
    void showCameraInfo(const glm::vec3& position, const glm::vec3& forward,
                       float fov, float nearPlane, float farPlane);

    // =========================================================================
    // ImGuizmo Integration
    // =========================================================================

    /**
     * @brief Begin gizmo frame (call after beginFrame)
     */
    void beginGizmo();

    /**
     * @brief Show transform gizmo for a model matrix
     * @param modelMatrix Matrix to manipulate (modified in place)
     * @param viewMatrix Camera view matrix
     * @param projMatrix Camera projection matrix
     * @param mode Gizmo mode (translate/rotate/scale)
     * @return true if gizmo was manipulated
     */
    bool showGizmo(glm::mat4& modelMatrix,
                   const glm::mat4& viewMatrix,
                   const glm::mat4& projMatrix,
                   GizmoMode mode);

    /**
     * @brief Show gizmo mode selector UI
     */
    void showGizmoModeSelector(GizmoMode& mode);

    // =========================================================================
    // Custom UI
    // =========================================================================

    /**
     * @brief Register a custom UI callback
     * Callbacks are invoked between beginFrame and endFrame
     */
    void addCallback(const std::string& name, ImGuiCallback callback);
    void removeCallback(const std::string& name);

    /**
     * @brief Set font scale
     */
    void setFontScale(float scale) { fontScale_ = scale; }

    [[nodiscard]] bool isInitialized() const { return initialized_; }
    [[nodiscard]] bool wantsCaptureKeyboard() const;
    [[nodiscard]] bool wantsCaptureMouse() const;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    SDL_Window* window_ = nullptr;

    float fontScale_ = 1.0f;
    bool initialized_ = false;
    bool frameStarted_ = false;

    std::vector<std::pair<std::string, ImGuiCallback>> callbacks_;

    bool createDescriptorPool(VkDevice device, uint32_t imageCount);
};

} // namespace jupiter::rendering
