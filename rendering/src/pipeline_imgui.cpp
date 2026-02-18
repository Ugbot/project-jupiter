/**
 * @file pipeline_imgui.cpp
 * @brief ImGui integration pipeline implementation
 */

#include "rendering/pipeline_imgui.h"
#include "logging/logging.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>

namespace jupiter::rendering {

// ============================================================================
// FrameStats
// ============================================================================

void FrameStats::update(float deltaTime) {
    deltaTimeMs = deltaTime * 1000.0f;
    currentFPS = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

    // Update FPS history
    fpsHistory[historyIndex] = currentFPS;
    historyIndex = (historyIndex + 1) % GRAPH_SIZE;

    // Calculate min/max/avg
    float sum = 0.0f;
    minFrameTimeMs = fpsHistory[0] > 0 ? 1000.0f / fpsHistory[0] : 0.0f;
    maxFrameTimeMs = 0.0f;

    for (size_t i = 0; i < GRAPH_SIZE; ++i) {
        if (fpsHistory[i] > 0.0f) {
            float frameTime = 1000.0f / fpsHistory[i];
            sum += frameTime;
            minFrameTimeMs = std::min(minFrameTimeMs, frameTime);
            maxFrameTimeMs = std::max(maxFrameTimeMs, frameTime);
        }
    }
    avgFrameTimeMs = sum / static_cast<float>(GRAPH_SIZE);
}

// ============================================================================
// PipelineImGui
// ============================================================================

PipelineImGui::~PipelineImGui() {
    destroy();
}

bool PipelineImGui::initialize(VkDevice device,
                               VkPhysicalDevice physicalDevice,
                               VkInstance instance,
                               VkQueue graphicsQueue,
                               uint32_t graphicsQueueFamily,
                               SDL_Window* window,
                               VkRenderPass renderPass,
                               VkFormat swapchainFormat,
                               uint32_t imageCount) {
    device_ = device;
    renderPass_ = renderPass;
    window_ = window;

    // Create descriptor pool for ImGui
    if (!createDescriptorPool(device, imageCount)) {
        LOG_ERROR("ImGui", "Failed to create descriptor pool");
        return false;
    }

    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.85f);

    // Initialize SDL3 backend
    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        LOG_ERROR("ImGui", "Failed to initialize SDL3 backend");
        return false;
    }

    // Load Vulkan functions for ImGui (needed when VK_NO_PROTOTYPES is set)
    auto loader = [](const char* function_name, void* user_data) -> PFN_vkVoidFunction {
        VkInstance inst = static_cast<VkInstance>(user_data);
        return vkGetInstanceProcAddr(inst, function_name);
    };
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_2, loader, instance);

    // Initialize Vulkan backend
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_2;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = graphicsQueueFamily;
    initInfo.Queue = graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = descriptorPool_;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;

    // Pipeline settings (new API)
    initInfo.PipelineInfoMain.RenderPass = renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        LOG_ERROR("ImGui", "Failed to initialize Vulkan backend");
        return false;
    }

    initialized_ = true;
    LOG_INFO("ImGui", "ImGui initialized successfully with SDL3+Vulkan backend");
    return true;
}

void PipelineImGui::destroy() {
    if (!initialized_) return;

    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    window_ = nullptr;
    initialized_ = false;

    LOG_INFO("ImGui", "ImGui destroyed");
}

bool PipelineImGui::createDescriptorPool(VkDevice device, uint32_t imageCount) {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    return vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) == VK_SUCCESS;
}

void PipelineImGui::beginFrame() {
    if (!initialized_) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (fontScale_ != 1.0f) {
        ImGui::GetIO().FontGlobalScale = fontScale_;
    }

    frameStarted_ = true;

    // Invoke registered callbacks
    for (const auto& [name, callback] : callbacks_) {
        callback();
    }
}

void PipelineImGui::endFrame() {
    if (!initialized_ || !frameStarted_) return;

    ImGui::Render();
    frameStarted_ = false;
}

void PipelineImGui::fillCommandBuffer(VkCommandBuffer cmd,
                                      VkFramebuffer framebuffer,
                                      VkExtent2D renderArea) {
    if (!initialized_) return;

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass_;
    rpInfo.framebuffer = framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = renderArea;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData) {
        ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    }

    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Built-in UI Windows
// ============================================================================

void PipelineImGui::showFrameStats(FrameStats& stats) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 150), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("FPS: %.1f", stats.currentFPS);
        ImGui::Text("Frame: %.2f ms", stats.deltaTimeMs);
        ImGui::Text("Avg: %.2f ms | Min: %.2f | Max: %.2f",
                   stats.avgFrameTimeMs, stats.minFrameTimeMs, stats.maxFrameTimeMs);

        ImGui::PlotLines("##FPS", stats.getGraph(),
                        static_cast<int>(stats.getGraphSize()),
                        static_cast<int>(stats.historyIndex),
                        nullptr, 0.0f, 120.0f, ImVec2(260, 50));
    }
    ImGui::End();
}

void PipelineImGui::showRendererSettings(float& exposure, float& gamma,
                                         bool& ssaoEnabled, bool& shadowsEnabled) {
    ImGui::SetNextWindowPos(ImVec2(10, 170), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 180), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Renderer Settings")) {
        ImGui::SliderFloat("Exposure", &exposure, 0.1f, 10.0f);
        ImGui::SliderFloat("Gamma", &gamma, 1.0f, 3.0f);
        ImGui::Separator();
        ImGui::Checkbox("SSAO", &ssaoEnabled);
        ImGui::Checkbox("Shadows", &shadowsEnabled);
    }
    ImGui::End();
}

void PipelineImGui::showLightEditor(glm::vec3& lightDir, glm::vec3& lightColor,
                                    float& lightIntensity) {
    ImGui::SetNextWindowPos(ImVec2(10, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 150), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Light Editor")) {
        ImGui::SliderFloat3("Direction", glm::value_ptr(lightDir), -1.0f, 1.0f);
        ImGui::ColorEdit3("Color", glm::value_ptr(lightColor));
        ImGui::SliderFloat("Intensity", &lightIntensity, 0.0f, 20.0f);
    }
    ImGui::End();
}

void PipelineImGui::showCameraInfo(const glm::vec3& position, const glm::vec3& forward,
                                   float fov, float nearPlane, float farPlane) {
    ImGui::SetNextWindowPos(ImVec2(10, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 120), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Camera")) {
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", position.x, position.y, position.z);
        ImGui::Text("Forward: (%.2f, %.2f, %.2f)", forward.x, forward.y, forward.z);
        ImGui::Text("FOV: %.1f | Near: %.2f | Far: %.1f", fov, nearPlane, farPlane);
    }
    ImGui::End();
}

// ============================================================================
// ImGuizmo Integration
// ============================================================================

void PipelineImGui::beginGizmo() {
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetGizmoSizeClipSpace(0.15f);
}

bool PipelineImGui::showGizmo(glm::mat4& modelMatrix,
                              const glm::mat4& viewMatrix,
                              const glm::mat4& projMatrix,
                              GizmoMode mode) {
    if (mode == GizmoMode::None) {
        return false;
    }

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    switch (mode) {
        case GizmoMode::Translate: op = ImGuizmo::TRANSLATE; break;
        case GizmoMode::Rotate:    op = ImGuizmo::ROTATE;    break;
        case GizmoMode::Scale:     op = ImGuizmo::SCALE;     break;
        default: break;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    // Need to flip Y for Vulkan projection
    glm::mat4 proj = projMatrix;
    proj[1][1] *= -1.0f;

    return ImGuizmo::Manipulate(
        glm::value_ptr(viewMatrix),
        glm::value_ptr(proj),
        op,
        ImGuizmo::WORLD,
        glm::value_ptr(modelMatrix));
}

void PipelineImGui::showGizmoModeSelector(GizmoMode& mode) {
    int modeInt = static_cast<int>(mode);

    ImGui::Text("Gizmo Mode:");
    ImGui::RadioButton("None", &modeInt, 0); ImGui::SameLine();
    ImGui::RadioButton("Translate", &modeInt, 1); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &modeInt, 2); ImGui::SameLine();
    ImGui::RadioButton("Scale", &modeInt, 3);

    mode = static_cast<GizmoMode>(modeInt);
}

// ============================================================================
// Custom UI
// ============================================================================

void PipelineImGui::addCallback(const std::string& name, ImGuiCallback callback) {
    // Remove existing callback with same name
    removeCallback(name);
    callbacks_.emplace_back(name, std::move(callback));
}

void PipelineImGui::removeCallback(const std::string& name) {
    callbacks_.erase(
        std::remove_if(callbacks_.begin(), callbacks_.end(),
            [&name](const auto& pair) { return pair.first == name; }),
        callbacks_.end());
}

bool PipelineImGui::wantsCaptureKeyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

bool PipelineImGui::wantsCaptureMouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

} // namespace jupiter::rendering
