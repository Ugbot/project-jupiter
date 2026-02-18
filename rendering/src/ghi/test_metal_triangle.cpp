/**
 * @file test_metal_triangle.cpp
 * @brief Test program for GHI Metal backend
 * 
 * Standalone test that renders a colored triangle using:
 * - GHI Metal backend
 * - metal-cpp C++ wrapper
 * - SDL3 window with CAMetalLayer
 * 
 * This proves the GHI Metal backend works before integrating into main application.
 */

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION  
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "rendering/ghi/ghi.h"
#include "backends/ghi_metal.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

using namespace jupiter::rendering::ghi;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    jupiter::logging::initialize();
    LOG_INFO("TestMetal", "Starting GHI Metal backend test");
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("TestMetal", "SDL init failed");
        return 1;
    }
    
    // Create window
    SDL_Window* window = SDL_CreateWindow(
        "GHI Metal Test - Triangle",
        800, 600,
        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        LOG_ERROR("TestMetal", "Failed to create window");
        SDL_Quit();
        return 1;
    }
    
    // Get Metal layer from SDL
    SDL_MetalView metalView = SDL_Metal_CreateView(window);
    CA::MetalLayer* metalLayer = (__bridge CA::MetalLayer*)SDL_Metal_GetLayer(metalView);
    
    LOG_INFO("TestMetal", "SDL window and Metal layer created");
    
    // Create GHI Metal backend directly
    GHI_MetalBackend metalBackend;
    
    if (!metalBackend.initialize()) {
        LOG_ERROR("TestMetal", "Failed to initialize Metal backend");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Connect layer to backend
    metalBackend.setMetalLayer(metalLayer);
    metalBackend.setDrawableSize(800, 600);
    
    LOG_INFO("TestMetal", "Metal backend initialized and connected");
    
    // Triangle vertices (position + color)
    struct Vertex {
        float pos[2];
        float color[3];
    };
    
    Vertex vertices[] = {
        {{  0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f }},  // Bottom - Red
        {{  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f }},  // Top Right - Green
        {{ -0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }}   // Top Left - Blue
    };
    
    // Create vertex buffer via GHI
    BufferHandle vertexBuffer = metalBackend.createBuffer({
        .type = BufferType::Vertex,
        .usage = BufferUsage::Static,
        .size = sizeof(vertices),
        .data = vertices
    });
    
    if (!vertexBuffer.isValid()) {
        LOG_ERROR("TestMetal", "Failed to create vertex buffer");
        metalBackend.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    LOG_INFO("TestMetal", "Vertex buffer created");
    
    // Load Metal shader
    ShaderHandle shader = metalBackend.createShader({
        .vertexPath = "shaders/metal/simple_triangle.metal"
    });
    
    if (!shader.isValid()) {
        LOG_ERROR("TestMetal", "Failed to load shader");
        metalBackend.destroyBuffer(vertexBuffer);
        metalBackend.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    LOG_INFO("TestMetal", "Shader loaded");
    
    // Set render state
    RenderState state;
    state.clearColor = glm::vec4(0.0f, 0.5f, 0.7f, 1.0f);  // Sky blue
    state.depthTestEnabled = false;  // No depth for simple triangle
    metalBackend.setRenderState(state);
    
    LOG_INFO("TestMetal", "Entering main loop");
    
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
        
        // Render via GHI
        metalBackend.beginFrame();
        metalBackend.beginRenderPass();
        
        // Set viewport
        metalBackend.setViewport(0, 0, 800, 600);
        
        // Bind resources
        metalBackend.bindVertexBuffer(vertexBuffer, 0, 0);
        
        // Draw triangle
        metalBackend.draw(3, 1, 0, 0);
        
        metalBackend.endRenderPass();
        metalBackend.endFrame();
    }
    
    LOG_INFO("TestMetal", "Shutting down");
    
    // Cleanup
    metalBackend.destroyBuffer(vertexBuffer);
    metalBackend.destroyShader(shader);
    metalBackend.shutdown();
    
    SDL_Metal_DestroyView(metalView);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    LOG_INFO("TestMetal", "Test complete");
    return 0;
}

