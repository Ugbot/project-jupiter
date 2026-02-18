/**
 * @file main_ghi.cpp
 * @brief Landscape Demo - GHI/RAL Version
 * 
 * Demonstrates:
 * - GHI/RAL rendering abstraction
 * - Terrain mesh rendering
 * - GPU grass system (compute)
 * - Trail system (compute)
 * 
 * This is the new cross-platform version using GHI instead of raw Vulkan.
 */

#include "rendering/ghi.h"
#include "rendering/ral/ral.h"
#include "rendering/ral/trail_field.h"
#include "rendering/ral/grass_system.h"
#include "rendering/pipelines/pipeline_simple.h"
#include "rendering/primitives.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <chrono>

#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

using namespace jupiter;
using namespace jupiter::rendering;

// ============================================================================
// Simple Fly Camera
// ============================================================================

class FlyCamera {
public:
    void setPosition(const glm::vec3& pos) { position_ = pos; }
    void setYawPitch(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }
    
    glm::vec3 getPosition() const { return position_; }
    glm::vec3 getForward() const {
        return glm::normalize(glm::vec3(
            std::cos(pitch_) * std::sin(yaw_),
            std::sin(pitch_),
            std::cos(pitch_) * std::cos(yaw_)
        ));
    }
    glm::vec3 getRight() const {
        return glm::normalize(glm::cross(getForward(), glm::vec3(0, 1, 0)));
    }

    void processInput(const bool* keyState, float deltaTime) {
        float velocity = moveSpeed_ * deltaTime;
        if (keyState[SDL_SCANCODE_LSHIFT]) velocity *= 3.0f;
        if (keyState[SDL_SCANCODE_LCTRL]) velocity *= 0.25f;

        glm::vec3 forward = getForward();
        glm::vec3 right = getRight();

        if (keyState[SDL_SCANCODE_W]) position_ += forward * velocity;
        if (keyState[SDL_SCANCODE_S]) position_ -= forward * velocity;
        if (keyState[SDL_SCANCODE_A]) position_ -= right * velocity;
        if (keyState[SDL_SCANCODE_D]) position_ += right * velocity;
        if (keyState[SDL_SCANCODE_SPACE]) position_.y += velocity;
        if (keyState[SDL_SCANCODE_C]) position_.y -= velocity;
    }

    void processMouse(float xoffset, float yoffset) {
        yaw_ -= xoffset * mouseSensitivity_;
        pitch_ -= yoffset * mouseSensitivity_;
        pitch_ = glm::clamp(pitch_, glm::radians(-89.0f), glm::radians(89.0f));
    }

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position_, position_ + getForward(), glm::vec3(0, 1, 0));
    }

private:
    glm::vec3 position_ = glm::vec3(0.0f, 10.0f, 50.0f);
    float yaw_ = glm::radians(180.0f);
    float pitch_ = 0.0f;
    float moveSpeed_ = 15.0f;
    float mouseSensitivity_ = 0.002f;
};

// ============================================================================
// Parse Command Line
// ============================================================================

ghi::Backend parseBackend(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--backend=") == 0) {
            std::string backend = arg.substr(10);
            if (backend == "metal") {
#ifdef __APPLE__
                return ghi::Backend::Metal;
#else
                return ghi::Backend::Vulkan;
#endif
            } else if (backend == "vulkan") {
                return ghi::Backend::Vulkan;
            }
        }
    }
#ifdef __APPLE__
    return ghi::Backend::Metal;
#else
    return ghi::Backend::Vulkan;
#endif
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    logging::initialize();
    
    ghi::Backend backend = parseBackend(argc, argv);
    
    std::cout << R"(
╔══════════════════════════════════════════════════════════════════╗
║       LANDSCAPE DEMO (GHI/RAL)                                   ║
║                                                                  ║
║  Backend: )" << ghi::getBackendName(backend) << R"(
║                                                                  ║
║  Controls:                                                       ║
║    WASD - Move        SPACE/C - Up/Down                         ║
║    Mouse - Look       ESC - Exit                                 ║
╚══════════════════════════════════════════════════════════════════╝
)" << std::endl;

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("LandscapeDemo", "SDL init failed");
        return 1;
    }

    // Create window
    SDL_Window* window = nullptr;
    SDL_MetalView metalView = nullptr;

    if (backend == ghi::Backend::Metal) {
#ifdef __APPLE__
        window = SDL_CreateWindow("Landscape Demo - Metal", 1280, 720,
                                   SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE);
        if (window) {
            metalView = SDL_Metal_CreateView(window);
        }
#endif
    } else {
        window = SDL_CreateWindow("Landscape Demo - Vulkan", 1280, 720,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    }

    if (!window) {
        LOG_ERROR("LandscapeDemo", "Failed to create window");
        SDL_Quit();
        return 1;
    }

    // Initialize GHI
    if (!ghi::initialize(backend)) {
        LOG_ERROR("LandscapeDemo", "Failed to initialize GHI");
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Connect surface
#ifdef __APPLE__
    if (backend == ghi::Backend::Metal && metalView) {
        void* layer = SDL_Metal_GetLayer(metalView);
        ghi::setMetalLayer(layer);
        ghi::setMetalDrawableSize(1280, 720);
    }
#endif

    if (backend == ghi::Backend::Vulkan) {
        VkSurfaceKHR surface;
        VkInstance instance = static_cast<VkInstance>(ghi::getVulkanInstance());
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            LOG_ERROR("LandscapeDemo", "Failed to create Vulkan surface");
            ghi::shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        ghi::setVulkanSurface(static_cast<void*>(surface), 1280, 720);
    }

    // Initialize RAL
    if (!ral::initialize()) {
        LOG_ERROR("LandscapeDemo", "Failed to initialize RAL");
        ghi::shutdown();
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    LOG_INFO("LandscapeDemo", "GHI/RAL initialized");

    // Create SimplePipeline (or PBRPipeline when shaders are ready)
    SimplePipeline pipeline;
    if (!pipeline.initialize(backend)) {
        LOG_ERROR("LandscapeDemo", "Failed to initialize pipeline");
    }

    // Create test geometry (terrain placeholder - plane)
    auto planeMesh = primitives::createPlane(100.0f, 100.0f, 10);
    ghi::BufferHandle planeVBO = planeMesh.createVertexBuffer();
    ghi::BufferHandle planeIBO = planeMesh.createIndexBuffer();

    // Create camera
    FlyCamera camera;
    camera.setPosition(glm::vec3(0.0f, 10.0f, 50.0f));
    camera.setYawPitch(glm::radians(180.0f), glm::radians(-10.0f));

    // Initialize trail field (optional - if compute works)
    ral::TrailField trailField;
    if (ghi::hasComputeShaders()) {
        ral::TrailFieldConfig trailConfig;
        if (!trailField.initialize(trailConfig)) {
            LOG_WARN("LandscapeDemo", "Trail field init failed - continuing without trails");
        }
    }

    // Initialize grass system (optional - if compute works)
    ral::GrassSystem grassSystem;
    if (ghi::hasComputeShaders()) {
        if (!grassSystem.initialize()) {
            LOG_WARN("LandscapeDemo", "Grass system init failed - continuing without grass");
        }
    }

    // Main loop
    bool running = true;
    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(window, mouseCaptured);

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Process events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                }
                if (event.key.scancode == SDL_SCANCODE_TAB) {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured) {
                camera.processMouse(event.motion.xrel, event.motion.yrel);
            }
        }

        // Update camera
        const bool* keyState = SDL_GetKeyboardState(nullptr);
        camera.processInput(keyState, deltaTime);

        // Update trail field (if player moved)
        glm::vec3 pos = camera.getPosition();
        glm::vec2 trailOrigin(pos.x - 128.0f, pos.z - 128.0f);
        trailField.update(deltaTime, trailOrigin);

        // Update grass
        grassSystem.generateInstances(camera.getPosition(), deltaTime);

        // Set camera
        ral::CameraInfo cameraInfo;
        cameraInfo.position = camera.getPosition();
        cameraInfo.viewMatrix = camera.getViewMatrix();
        cameraInfo.projectionMatrix = glm::perspective(
            glm::radians(60.0f), 1280.0f / 720.0f, 0.1f, 1000.0f);
        
        // Note: Vulkan Y-flip handled by viewport (negative height)

        pipeline.setCamera(cameraInfo);
        pipeline.setDirectionalLight(glm::vec3(-0.4f, -0.8f, -0.3f), 
                                      glm::vec3(1.0f, 0.95f, 0.85f), 3.0f);

        // Begin frame
        ghi::beginFrame();
        ghi::beginRenderPass();
        
        pipeline.beginFrame();

        // Set viewport
        ghi::setViewport(0, 0, 1280, 720);
        ghi::setScissor(0, 0, 1280, 720);

        // Render terrain plane
        ghi::bindVertexBuffer(planeVBO, 0, 0);
        ghi::bindIndexBuffer(planeIBO, 0);
        ghi::drawIndexed(static_cast<uint32_t>(planeMesh.indices.size()), 1, 0, 0, 0);

        // Draw grass (if enabled)
        // grassSystem.draw();

        pipeline.endFrame();

        ghi::endRenderPass();
        ghi::endFrame();
    }

    // Cleanup
    LOG_INFO("LandscapeDemo", "Shutting down...");

    grassSystem.shutdown();
    trailField.shutdown();
    pipeline.shutdown();

    ghi::destroyBuffer(planeVBO);
    ghi::destroyBuffer(planeIBO);

    ral::shutdown();
    ghi::shutdown();

    if (metalView) SDL_Metal_DestroyView(metalView);
    SDL_DestroyWindow(window);
    SDL_Quit();

    LOG_INFO("LandscapeDemo", "Clean exit");
    return 0;
}
