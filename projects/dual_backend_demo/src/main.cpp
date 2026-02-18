/**
 * @file main.cpp
 * @brief Dual Backend Demo - Metal or Vulkan via CLI
 * 
 * Demonstrates GHI/RAL multi-backend architecture.
 * 
 * Usage:
 *   ./dual_backend_demo --backend=metal    # Use native Metal
 *   ./dual_backend_demo --backend=vulkan   # Use Vulkan (MoltenVK on Mac)
 *   ./dual_backend_demo                    # Auto-detect (Metal on Mac, Vulkan elsewhere)
 */

#include "rendering/ghi.h"  // Everything rendering-related (GHI, RAL, GLM, primitives)
#include "logging/logging.h"
#include "platform/platform.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <string>
#include <chrono>

#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif

#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

using namespace jupiter;

// Parse command line arguments
rendering::ghi::Backend parseBackend(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg.find("--backend=") == 0) {
            std::string backend = arg.substr(10);
            
            if (backend == "metal") {
#ifdef __APPLE__
                return rendering::ghi::Backend::Metal;
#else
                std::cout << "Metal not available on this platform, using Vulkan\n";
                return rendering::ghi::Backend::Vulkan;
#endif
            } else if (backend == "vulkan") {
                return rendering::ghi::Backend::Vulkan;
            } else if (backend == "opengl") {
                std::cout << "OpenGL backend not yet implemented, using Vulkan\n";
                return rendering::ghi::Backend::Vulkan;
            }
        }
    }
    
    // Auto-detect
#ifdef __APPLE__
    return rendering::ghi::Backend::Metal;  // Prefer native Metal on macOS
#else
    return rendering::ghi::Backend::Vulkan;
#endif
}

int main(int argc, char* argv[]) {
    logging::initialize();
    
    // Parse backend choice
    rendering::ghi::Backend backend = parseBackend(argc, argv);
    
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       GHI/RAL Dual-Backend Demo                      ║\n";
    std::cout << "║                                                          ║\n";
    std::cout << "║  Backend: " << rendering::ghi::getBackendName(backend) << std::string(43 - strlen(rendering::ghi::getBackendName(backend)), ' ') << "║\n";
    std::cout << "║                                                          ║\n";
    std::cout << "║  Controls:                                               ║\n";
    std::cout << "║    ESC - Exit                                            ║\n";
    std::cout << "║                                                          ║\n";
    std::cout << "║  Try:                                                    ║\n";
    std::cout << "║    --backend=metal   (macOS native)                      ║\n";
    std::cout << "║    --backend=vulkan  (via MoltenVK on Mac)               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("DualBackend", "SDL init failed");
        return 1;
    }
    
    // Create window based on backend
    SDL_Window* window = nullptr;
    SDL_MetalView metalView = nullptr;
    
    if (backend == rendering::ghi::Backend::Metal) {
#ifdef __APPLE__
        window = SDL_CreateWindow(
            "Dual Backend Demo - Metal",
            1024, 768,
            SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE
        );
        
        if (window) {
            metalView = SDL_Metal_CreateView(window);
        }
#endif
    } else {
        window = SDL_CreateWindow(
            "Dual Backend Demo - Vulkan",
            1024, 768,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
        );
    }
    
    if (!window) {
        LOG_ERROR("DualBackend", "Failed to create window");
        SDL_Quit();
        return 1;
    }
    
    LOG_INFO("DualBackend", "Window created for %s backend", rendering::ghi::getBackendName(backend));
    
    // Initialize GHI
    if (!rendering::ghi::initialize(backend)) {
        LOG_ERROR("DualBackend", "Failed to initialize GHI backend");
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    LOG_INFO("DualBackend", "GHI initialized");
    
    // Connect backend-specific surface BEFORE RAL (shaders need render pass)
#ifdef __APPLE__
    if (backend == rendering::ghi::Backend::Metal && metalView) {
        void* layer = SDL_Metal_GetLayer(metalView);
        rendering::ghi::setMetalLayer(layer);
        rendering::ghi::setMetalDrawableSize(1024, 768);
        LOG_INFO("DualBackend", "Metal layer connected to GHI");
    }
#endif
    
    if (backend == rendering::ghi::Backend::Vulkan) {
        VkSurfaceKHR surface;
        VkInstance instance = static_cast<VkInstance>(rendering::ghi::getVulkanInstance());
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            LOG_ERROR("DualBackend", "Failed to create Vulkan surface: %s", SDL_GetError());
            rendering::ghi::shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        rendering::ghi::setVulkanSurface(static_cast<void*>(surface), 1024, 768);
        LOG_INFO("DualBackend", "Vulkan surface connected to GHI (swapchain + render pass ready)");
    }
    
    // Initialize RAL (now surface/render pass exist for shader creation)
    if (!rendering::ral::initialize()) {
        LOG_ERROR("DualBackend", "Failed to initialize RAL");
        rendering::ghi::shutdown();
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    LOG_INFO("DualBackend", "RAL initialized with SimplePipeline");
    
    // Set up camera
    rendering::ral::CameraInfo camera;
    camera.position = glm::vec3(0, 0, 5);
    camera.target = glm::vec3(0, 0, 0);
    camera.up = glm::vec3(0, 1, 0);
    camera.fov = 60.0f;
    camera.aspectRatio = 1024.0f / 768.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;
    
    // Compute view and projection matrices
    camera.viewMatrix = glm::lookAt(camera.position, camera.target, camera.up);
    camera.projectionMatrix = glm::perspective(glm::radians(camera.fov), camera.aspectRatio, camera.nearPlane, camera.farPlane);
    camera.viewProjectionMatrix = camera.projectionMatrix * camera.viewMatrix;
    
    rendering::ral::setCamera(camera);
    
    LOG_INFO("DualBackend", "Camera set up at position (%.1f, %.1f, %.1f) looking at (%.1f, %.1f, %.1f)",
             camera.position.x, camera.position.y, camera.position.z,
             camera.target.x, camera.target.y, camera.target.z);
    
    // Set up lighting
    rendering::ral::setAmbientLight(glm::vec3(0.3f, 0.35f, 0.4f), 1.0f);
    rendering::ral::createDirectionalLight(
        glm::vec3(-0.4f, -0.8f, -0.3f),
        glm::vec3(1.0f, 0.95f, 0.85f),
        2.5f
    );
    
    LOG_INFO("DualBackend", "Scene setup complete");
    
    // Create 3D cube geometry using rendering layer primitives
    rendering::primitives::MeshData cubeMesh = rendering::primitives::createCube();
    
    rendering::ghi::BufferHandle cubeVertexBuffer = cubeMesh.createVertexBuffer();
    rendering::ghi::BufferHandle cubeIndexBuffer = cubeMesh.createIndexBuffer();
    
    if (!cubeVertexBuffer.isValid() || !cubeIndexBuffer.isValid()) {
        LOG_ERROR("DualBackend", "Failed to create cube geometry buffers");
        rendering::ral::shutdown();
        rendering::ghi::shutdown();
        if (metalView) SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Create model matrix buffer (will rotate cube over time)
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    rendering::ghi::BufferHandle modelMatrixBuffer = rendering::ghi::createBuffer({
        .type = rendering::ghi::BufferType::Uniform,
        .usage = rendering::ghi::BufferUsage::Dynamic,
        .size = sizeof(glm::mat4),
        .data = &modelMatrix
    });
    
    // Create material uniform buffer
    struct MaterialUniforms {
        glm::vec4 baseColor;
        float metallic;
        float roughness;
        float pad0;
        float pad1;
    };
    
    MaterialUniforms material{};
    material.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);  // Light gray
    material.metallic = 0.0f;
    material.roughness = 0.5f;
    
    rendering::ghi::BufferHandle materialBuffer = rendering::ghi::createBuffer({
        .type = rendering::ghi::BufferType::Uniform,
        .usage = rendering::ghi::BufferUsage::Static,
        .size = sizeof(MaterialUniforms),
        .data = &material
    });
    
    // Create dummy 1x1 white texture
    uint32_t whitePixel = 0xFFFFFFFF;
    rendering::ghi::TextureHandle whiteTexture = rendering::ghi::createTexture({
        .width = 1,
        .height = 1,
        .format = rendering::ghi::Format::RGBA8_UNORM,
        .minFilter = rendering::ghi::Filter::Nearest,
        .magFilter = rendering::ghi::Filter::Nearest,
        .wrapS = rendering::ghi::WrapMode::Repeat,
        .wrapT = rendering::ghi::WrapMode::Repeat,
        .data = &whitePixel
    });
    
    // Create sphere
    rendering::primitives::MeshData sphereMesh = rendering::primitives::createSphere(0.5f, 32, 16);
    rendering::ghi::BufferHandle sphereVertexBuffer = sphereMesh.createVertexBuffer();
    rendering::ghi::BufferHandle sphereIndexBuffer = sphereMesh.createIndexBuffer();
    
    // Create plane (ground)
    rendering::primitives::MeshData planeMesh = rendering::primitives::createPlane(4.0f, 4.0f, 4);
    rendering::ghi::BufferHandle planeVertexBuffer = planeMesh.createVertexBuffer();
    rendering::ghi::BufferHandle planeIndexBuffer = planeMesh.createIndexBuffer();
    
    LOG_INFO("DualBackend", "Created test scene:");
    LOG_INFO("DualBackend", "  Cube: %zu vertices, %zu indices", cubeMesh.vertices.size(), cubeMesh.indices.size());
    LOG_INFO("DualBackend", "  Sphere: %zu vertices, %zu indices", sphereMesh.vertices.size(), sphereMesh.indices.size());
    LOG_INFO("DualBackend", "  Plane: %zu vertices, %zu indices", planeMesh.vertices.size(), planeMesh.indices.size());
    
    // Print backend capabilities
    const auto& caps = rendering::ghi::getCapabilities();
    std::cout << "\nBackend Capabilities:\n";
    std::cout << "  Device: " << caps.deviceName.c_str() << "\n";
    std::cout << "  Compute shaders: " << (caps.hasComputeShaders ? "yes" : "no") << "\n";
    std::cout << "  Indirect draw: " << (caps.hasIndirectDraw ? "yes" : "no") << "\n";
    std::cout << "  Subgroups/SIMD: " << (caps.hasSubgroups ? "yes" : "no");
    if (caps.hasSubgroups) std::cout << " (size=" << caps.subgroupSize << ")";
    std::cout << "\n";
    
    if (backend == rendering::ghi::Backend::Metal) {
        std::cout << "  Tile shaders: " << (caps.hasTileShaders ? "yes" : "no") << "\n";
        std::cout << "  Memoryless textures: " << (caps.hasMemorylessTextures ? "yes" : "no") << "\n";
    }
    
    std::cout << "\nEntering render loop...\n";
    std::cout << "Backend: " << rendering::ghi::getBackendName(backend) << "\n";
    std::cout << "Press ESC to exit\n\n";
    std::cout << "Window should be showing sky blue background.\n";
    std::cout << "If you see black, the rendering path needs completion.\n\n";
    
    // Main loop
    bool quit = false;
    SDL_Event event;
    uint32_t frameCount = 0;
    
    // Give window time to appear
    SDL_Delay(100);
    
    LOG_INFO("DualBackend", "Starting render loop");
    
    float rotationAngle = 0.0f;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    while (!quit) {
        // Poll events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                LOG_INFO("DualBackend", "Quit event received");
                quit = true;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    LOG_INFO("DualBackend", "ESC pressed, exiting");
                    quit = true;
                }
            }
        }
        
        // Update rotation for cube
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - startTime).count();
        rotationAngle = deltaTime * 45.0f;  // 45 degrees per second
        
        // Render via RAL
        rendering::ral::beginFrame();
        
        // === Cube (center, rotating) ===
        glm::mat4 cubeModel = glm::rotate(glm::mat4(1.0f), glm::radians(rotationAngle), glm::vec3(0.0f, 1.0f, 0.0f));
        cubeModel = glm::rotate(cubeModel, glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        rendering::ghi::updateBuffer(modelMatrixBuffer, 0, sizeof(glm::mat4), &cubeModel);
        
        rendering::ghi::bindVertexBuffer(cubeVertexBuffer, 0, 0);
        rendering::ghi::bindIndexBuffer(cubeIndexBuffer, 0);
        rendering::ghi::setPushConstants(&cubeModel, sizeof(glm::mat4));
        rendering::ghi::drawIndexed(cubeMesh.indices.size(), 1, 0, 0, 0);
        
        // === Sphere (left, static) ===
        glm::mat4 sphereModel = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f));
        sphereModel = glm::scale(sphereModel, glm::vec3(0.8f));
        
        rendering::ghi::bindVertexBuffer(sphereVertexBuffer, 0, 0);
        rendering::ghi::bindIndexBuffer(sphereIndexBuffer, 0);
        rendering::ghi::setPushConstants(&sphereModel, sizeof(glm::mat4));
        rendering::ghi::drawIndexed(sphereMesh.indices.size(), 1, 0, 0, 0);
        
        // === Plane (ground, below) ===
        glm::mat4 planeModel = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f));
        
        rendering::ghi::bindVertexBuffer(planeVertexBuffer, 0, 0);
        rendering::ghi::bindIndexBuffer(planeIndexBuffer, 0);
        rendering::ghi::setPushConstants(&planeModel, sizeof(glm::mat4));
        rendering::ghi::drawIndexed(planeMesh.indices.size(), 1, 0, 0, 0);
        
        rendering::ral::endFrame();
        
        frameCount++;
        
        // Log every 60 frames
        if (frameCount % 60 == 0) {
            LOG_INFO("DualBackend", "Frame %u rendered via %s", frameCount, 
                     rendering::ghi::getBackendName(backend));
        }
        
        // Small delay to not peg CPU
        SDL_Delay(16);  // ~60 FPS
    }
    
    LOG_INFO("DualBackend", "Exited render loop after %u frames", frameCount);
    
    LOG_INFO("DualBackend", "Rendered %u frames total", frameCount);
    LOG_INFO("DualBackend", "Shutting down");
    
    // Cleanup
    rendering::ghi::destroyBuffer(cubeVertexBuffer);
    rendering::ghi::destroyBuffer(cubeIndexBuffer);
    rendering::ghi::destroyBuffer(sphereVertexBuffer);
    rendering::ghi::destroyBuffer(sphereIndexBuffer);
    rendering::ghi::destroyBuffer(planeVertexBuffer);
    rendering::ghi::destroyBuffer(planeIndexBuffer);
    rendering::ghi::destroyBuffer(modelMatrixBuffer);
    rendering::ghi::destroyBuffer(materialBuffer);
    rendering::ghi::destroyTexture(whiteTexture);
    
    rendering::ral::shutdown();
    rendering::ghi::shutdown();
    
    if (metalView) {
        SDL_Metal_DestroyView(metalView);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    LOG_INFO("DualBackend", "Demo complete - backend was: %s", rendering::ghi::getBackendName(backend));
    
    return 0;
}

