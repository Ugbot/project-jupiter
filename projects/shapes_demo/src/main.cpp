/**
 * @file main.cpp
 * @brief Shapes Demo - Animated shapes on both Metal and Vulkan
 * 
 * Minimal demo showing:
 * - Multiple animated shapes (cubes, spheres)
 * - Color-coded by object
 * - Works identically on Metal and Vulkan
 */

#include "rendering/ghi.h"
#include "rendering/ral/ral.h"
#include "rendering/primitives.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <chrono>
#include <cmath>

#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

using namespace jupiter;
using namespace jupiter::rendering;

// ============================================================================
// Backend Selection
// ============================================================================

ghi::Backend parseBackend(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--backend=") == 0) {
            std::string backend = arg.substr(10);
            if (backend == "vulkan") return ghi::Backend::Vulkan;
            if (backend == "metal") {
#ifdef __APPLE__
                return ghi::Backend::Metal;
#endif
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
    const int WIDTH = 1280;
    const int HEIGHT = 720;
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════╗\n";
    std::cout << "║          SHAPES DEMO                          ║\n";
    std::cout << "║  Backend: " << ghi::getBackendName(backend) << "                              \n";
    std::cout << "║  Press ESC to exit                            ║\n";
    std::cout << "╚═══════════════════════════════════════════════╝\n\n";

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("ShapesDemo", "SDL init failed: %s", SDL_GetError());
        return 1;
    }

    // Create window
    SDL_Window* window = nullptr;
    SDL_MetalView metalView = nullptr;

    if (backend == ghi::Backend::Metal) {
#ifdef __APPLE__
        window = SDL_CreateWindow("Shapes Demo - Metal", WIDTH, HEIGHT,
                                   SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE);
        if (window) metalView = SDL_Metal_CreateView(window);
#endif
    } else {
        window = SDL_CreateWindow("Shapes Demo - Vulkan", WIDTH, HEIGHT,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    }

    if (!window) {
        LOG_ERROR("ShapesDemo", "Failed to create window");
        SDL_Quit();
        return 1;
    }

    // Initialize GHI
    if (!ghi::initialize(backend)) {
        LOG_ERROR("ShapesDemo", "Failed to initialize GHI");
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
        ghi::setMetalDrawableSize(WIDTH, HEIGHT);
    }
#endif

    if (backend == ghi::Backend::Vulkan) {
        VkSurfaceKHR surface;
        VkInstance instance = static_cast<VkInstance>(ghi::getVulkanInstance());
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            LOG_ERROR("ShapesDemo", "Failed to create Vulkan surface");
            ghi::shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        ghi::setVulkanSurface(static_cast<void*>(surface), WIDTH, HEIGHT);
    }

    // Initialize RAL
    if (!ral::initialize()) {
        LOG_ERROR("ShapesDemo", "Failed to initialize RAL");
        ghi::shutdown();
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    LOG_INFO("ShapesDemo", "GHI/RAL initialized on %s", ghi::getBackendName(backend));

    // ========================================================================
    // Create Camera
    // ========================================================================
    
    ral::CameraInfo camera;
    camera.position = glm::vec3(0.0f, 3.0f, 8.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.up = glm::vec3(0, 1, 0);
    camera.fov = 60.0f;
    camera.aspectRatio = static_cast<float>(WIDTH) / HEIGHT;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    
    camera.viewMatrix = glm::lookAt(camera.position, camera.target, camera.up);
    camera.projectionMatrix = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, 
                                                camera.nearPlane, camera.farPlane);
    
    // Note: Vulkan Y-flip is now handled by viewport (negative height)
    // No projection matrix modification needed
    
    ral::setCamera(camera);

    // ========================================================================
    // Create Geometry
    // ========================================================================
    
    // Create cubes
    primitives::MeshData cubeMesh = primitives::createCube();
    ghi::BufferHandle cubeVBO = cubeMesh.createVertexBuffer();
    ghi::BufferHandle cubeIBO = cubeMesh.createIndexBuffer();
    
    // Create sphere
    primitives::MeshData sphereMesh = primitives::createSphere(0.6f, 24, 12);
    ghi::BufferHandle sphereVBO = sphereMesh.createVertexBuffer();
    ghi::BufferHandle sphereIBO = sphereMesh.createIndexBuffer();
    
    // Create ground plane
    primitives::MeshData planeMesh = primitives::createPlane(8.0f, 8.0f, 4);
    ghi::BufferHandle planeVBO = planeMesh.createVertexBuffer();
    ghi::BufferHandle planeIBO = planeMesh.createIndexBuffer();
    
    // Model matrix buffer (reused for each object)
    glm::mat4 identity = glm::mat4(1.0f);
    ghi::BufferHandle modelBuffer = ghi::createBuffer({
        .type = ghi::BufferType::Uniform,
        .usage = ghi::BufferUsage::Dynamic,
        .size = sizeof(glm::mat4),
        .data = &identity
    });

    if (!cubeVBO.isValid() || !sphereVBO.isValid() || !planeVBO.isValid()) {
        LOG_ERROR("ShapesDemo", "Failed to create geometry buffers");
        ral::shutdown();
        ghi::shutdown();
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    LOG_INFO("ShapesDemo", "Created geometry: cube(%zu verts), sphere(%zu verts), plane(%zu verts)",
             cubeMesh.vertices.size(), sphereMesh.vertices.size(), planeMesh.vertices.size());

    // ========================================================================
    // Main Loop
    // ========================================================================
    
    bool running = true;
    auto startTime = std::chrono::high_resolution_clock::now();
    uint32_t frameCount = 0;

    LOG_INFO("ShapesDemo", "Starting render loop...");

    while (running) {
        // Events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
        }

        // Time
        auto now = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float>(now - startTime).count();

        // Begin frame
        ral::beginFrame();

        // ====================================================================
        // Draw Ground Plane
        // ====================================================================
        glm::mat4 planeModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f));
        
        ghi::bindVertexBuffer(planeVBO, 0, 0);
        ghi::bindIndexBuffer(planeIBO, 0);
        ghi::setPushConstants(&planeModel, sizeof(glm::mat4));
        ghi::drawIndexed(static_cast<uint32_t>(planeMesh.indices.size()), 1, 0, 0, 0);

        // ====================================================================
        // Draw Center Cube (rotating on Y axis)
        // ====================================================================
        float cubeRotation = time * 45.0f;  // 45 deg/sec
        glm::mat4 cubeModel = glm::rotate(glm::mat4(1.0f), glm::radians(cubeRotation), glm::vec3(0, 1, 0));
        cubeModel = glm::rotate(cubeModel, glm::radians(15.0f), glm::vec3(1, 0, 0));
        
        ghi::bindVertexBuffer(cubeVBO, 0, 0);
        ghi::bindIndexBuffer(cubeIBO, 0);
        ghi::setPushConstants(&cubeModel, sizeof(glm::mat4));
        ghi::drawIndexed(static_cast<uint32_t>(cubeMesh.indices.size()), 1, 0, 0, 0);

        // ====================================================================
        // Draw Orbiting Spheres (3 spheres orbiting center)
        // ====================================================================
        for (int i = 0; i < 3; ++i) {
            float angle = time * 60.0f + i * 120.0f;  // 60 deg/sec, spaced 120 apart
            float radius = 3.0f;
            float x = std::cos(glm::radians(angle)) * radius;
            float z = std::sin(glm::radians(angle)) * radius;
            float y = std::sin(time * 2.0f + i) * 0.5f;  // Bobbing up/down
            
            glm::mat4 sphereModel = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
            
            ghi::bindVertexBuffer(sphereVBO, 0, 0);
            ghi::bindIndexBuffer(sphereIBO, 0);
            ghi::setPushConstants(&sphereModel, sizeof(glm::mat4));
            ghi::drawIndexed(static_cast<uint32_t>(sphereMesh.indices.size()), 1, 0, 0, 0);
        }

        // ====================================================================
        // Draw Corner Cubes (4 smaller rotating cubes)
        // ====================================================================
        float corners[][2] = {{-2.5f, -2.5f}, {2.5f, -2.5f}, {-2.5f, 2.5f}, {2.5f, 2.5f}};
        for (int i = 0; i < 4; ++i) {
            float rot = time * (90.0f + i * 20.0f);  // Different speeds
            glm::mat4 cornerModel = glm::translate(glm::mat4(1.0f), 
                                                    glm::vec3(corners[i][0], 0.0f, corners[i][1]));
            cornerModel = glm::rotate(cornerModel, glm::radians(rot), glm::vec3(0, 1, 0));
            cornerModel = glm::rotate(cornerModel, glm::radians(rot * 0.7f), glm::vec3(1, 0, 0));
            cornerModel = glm::scale(cornerModel, glm::vec3(0.5f));
            
            ghi::bindVertexBuffer(cubeVBO, 0, 0);
            ghi::bindIndexBuffer(cubeIBO, 0);
            ghi::setPushConstants(&cornerModel, sizeof(glm::mat4));
            ghi::drawIndexed(static_cast<uint32_t>(cubeMesh.indices.size()), 1, 0, 0, 0);
        }

        // End frame
        ral::endFrame();
        
        frameCount++;
        
        // Log FPS every 2 seconds
        if (frameCount % 120 == 0) {
            float fps = frameCount / time;
            LOG_INFO("ShapesDemo", "Frame %u, %.1f FPS (%s)", frameCount, fps, 
                     ghi::getBackendName(backend));
        }

        SDL_Delay(16);  // ~60 FPS
    }

    // ========================================================================
    // Cleanup
    // ========================================================================
    
    LOG_INFO("ShapesDemo", "Shutting down after %u frames...", frameCount);

    ghi::destroyBuffer(cubeVBO);
    ghi::destroyBuffer(cubeIBO);
    ghi::destroyBuffer(sphereVBO);
    ghi::destroyBuffer(sphereIBO);
    ghi::destroyBuffer(planeVBO);
    ghi::destroyBuffer(planeIBO);
    ghi::destroyBuffer(modelBuffer);

    ral::shutdown();
    ghi::shutdown();

    if (metalView) SDL_Metal_DestroyView(metalView);
    SDL_DestroyWindow(window);
    SDL_Quit();

    LOG_INFO("ShapesDemo", "Clean exit");
    return 0;
}
