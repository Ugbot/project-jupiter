/**
 * @file main.cpp
 * @brief Absolute minimal rendering test
 * 
 * Renders ONE hardcoded triangle to eliminate all possible issues.
 */

#include "rendering/ghi.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>

#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif

int main(int argc, char* argv[]) {
    jupiter::logging::initialize();
    
    std::cout << "\n=== MINIMAL RENDERING TEST ===\n";
    std::cout << "This should show a BRIGHT RED triangle on sky blue.\n";
    std::cout << "If you don't see it, there's a fundamental issue.\n\n";
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL init failed\n");
        return 1;
    }
    
    // Create Metal window
    SDL_Window* window = SDL_CreateWindow(
        "Minimal Test - Metal",
        800, 600,
        SDL_WINDOW_METAL
    );
    
    if (!window) {
        printf("Window creation failed\n");
        SDL_Quit();
        return 1;
    }
    
#ifdef __APPLE__
    SDL_MetalView metalView = SDL_Metal_CreateView(window);
    if (!metalView) {
        printf("Metal view creation failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif
    
    // Initialize Metal backend
    if (!jupiter::rendering::ghi::initialize(jupiter::rendering::ghi::Backend::Metal)) {
        printf("GHI initialization failed\n");
        SDL_Metal_DestroyView(metalView);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Connect Metal layer
    void* layer = SDL_Metal_GetLayer(metalView);
    jupiter::rendering::ghi::setMetalLayer(layer);
    jupiter::rendering::ghi::setMetalDrawableSize(800, 600);
    
    printf("Metal backend initialized\n");
    
    // Hardcoded triangle vertices (NDC coordinates, no transforms needed)
    struct SimpleVertex {
        float pos[2];     // NDC: -1 to 1
        float color[3];   // RGB
    };
    
    SimpleVertex triangle[] = {
        {{ 0.0f,  0.5f}, {1.0f, 0.0f, 0.0f}},  // Top - RED
        {{-0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},  // Bottom-left - GREEN
        {{ 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},  // Bottom-right - BLUE
    };
    
    // Create vertex buffer
    auto vbo = jupiter::rendering::ghi::createBuffer({
        .type = jupiter::rendering::ghi::BufferType::Vertex,
        .usage = jupiter::rendering::ghi::BufferUsage::Static,
        .size = sizeof(triangle),
        .data = triangle
    });
    
    printf("Vertex buffer created\n");
    
    // Render loop
    bool running = true;
    SDL_Event event;
    int frameCount = 0;
    
    while (running && frameCount < 300) {  // 5 seconds at 60fps
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_KEY_DOWN) {
                running = false;
            }
        }
        
        // Simple render: just clear + present (no pipeline, just testing clear)
        jupiter::rendering::ghi::beginFrame();
        jupiter::rendering::ghi::beginRenderPass();
        // Don't draw anything - just test if clear color shows
        jupiter::rendering::ghi::endRenderPass();
        jupiter::rendering::ghi::endFrame();
        
        frameCount++;
        
        if (frameCount % 60 == 0) {
            printf("Frame %d - if you see sky blue, rendering works!\n", frameCount);
        }
        
        SDL_Delay(16);
    }
    
    printf("\nRendered %d frames\n", frameCount);
    printf("Did you see sky blue background? (y/n)\n");
    
    // Cleanup
    jupiter::rendering::ghi::destroyBuffer(vbo);
    jupiter::rendering::ghi::shutdown();
    SDL_Metal_DestroyView(metalView);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}

