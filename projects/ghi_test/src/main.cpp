/**
 * @file main.cpp
 * @brief GHI Test - Basic Forward Renderer Demo
 * 
 * Tests GHI/RAL with SimplePipeline on Metal and Vulkan.
 * Renders colored primitives with Lambertian lighting.
 */

#include "rendering/ghi/ghi.h"
#include "rendering/ral/ral.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>

#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif

using namespace jupiter;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    logging::initialize();
    LOG_INFO("GHI_Test", "Starting GHI/RAL test demo");
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("GHI_Test", "SDL init failed");
        return 1;
    }
    
    // Create window
    SDL_Window* window = nullptr;
    
#ifdef __APPLE__
    // Metal backend on macOS
    window = SDL_CreateWindow(
        "GHI Test - Metal",
        1024, 768,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        LOG_ERROR("GHI_Test", "Failed to create window");
        SDL_Quit();
        return 1;
    }
    
    // Initialize GHI with Metal
    if (!rendering::ghi::initialize(rendering::ghi::Backend::Metal)) {
        LOG_ERROR("GHI_Test", "Failed to initialize GHI Metal backend");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Get Metal layer and connect to GHI
    // TODO: Need to expose GHI backend to set Metal layer
    
#else
    // Vulkan backend on Linux/Windows
    window = SDL_CreateWindow(
        "GHI Test - Vulkan",
        1024, 768,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        LOG_ERROR("GHI_Test", "Failed to create window");
        SDL_Quit();
        return 1;
    }
    
    // Initialize GHI with Vulkan
    if (!rendering::ghi::initialize(rendering::ghi::Backend::Vulkan)) {
        LOG_ERROR("GHI_Test", "Failed to initialize GHI Vulkan backend");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif
    
    LOG_INFO("GHI_Test", "GHI backend initialized: %s", 
             rendering::ghi::getBackendName(rendering::ghi::getActiveBackend()));
    
    // Initialize RAL
    if (!rendering::ral::initialize()) {
        LOG_ERROR("GHI_Test", "Failed to initialize RAL");
        rendering::ghi::shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    LOG_INFO("GHI_Test", "RAL initialized with SimplePipeline");
    
    // Set camera
    rendering::ral::CameraInfo camera;
    camera.position = glm::vec3(0, 2, 10);
    camera.target = glm::vec3(0, 0, 0);
    camera.fov = 60.0f;
    camera.aspectRatio = 1024.0f / 768.0f;
    rendering::ral::setCamera(camera);
    
    // Set lighting
    rendering::ral::setAmbientLight(glm::vec3(0.3f, 0.35f, 0.4f), 1.0f);
    rendering::ral::createDirectionalLight(
        glm::vec3(-0.4f, -0.8f, -0.3f),  // Direction
        glm::vec3(1.0f, 0.95f, 0.85f),   // Warm sun
        2.5f                              // Intensity
    );
    
    LOG_INFO("GHI_Test", "Scene setup complete");
    
    // TODO: Create primitive meshes via RAL
    // For now, just render loop
    
    LOG_INFO("GHI_Test", "Entering main loop");
    LOG_INFO("GHI_Test", "Press ESC to exit");
    
    // Main loop
    bool quit = false;
    SDL_Event event;
    
    while (!quit) {
        // Poll events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                quit = true;
            }
        }
        
        // Render via RAL
        rendering::ral::beginFrame();
        
        // TODO: Render primitives
        // rendering::ral::renderMesh(cubeMesh, transform, material);
        
        rendering::ral::endFrame();
    }
    
    LOG_INFO("GHI_Test", "Shutting down");
    
    // Cleanup
    rendering::ral::shutdown();
    rendering::ghi::shutdown();
    
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    LOG_INFO("GHI_Test", "Test complete");
    return 0;
}

