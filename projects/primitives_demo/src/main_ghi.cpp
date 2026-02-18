/**
 * @file main_ghi.cpp
 * @brief Cross-Platform Primitives Demo using GHI/RAL
 * 
 * Demonstrates the Jupiter engine's cross-platform rendering:
 * - Works on Metal (macOS), Vulkan (all), and OpenGL (fallback)
 * - Uses the GHI abstraction layer
 * - Uses the RAL for high-level mesh/material management
 * 
 * Build with: -DUSE_GHI_DEMO=ON
 */

#include "rendering/ghi/ghi.h"
#include "rendering/ral/ral.h"
#include "logging/logging.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdlib>

#include "imgui_overlay_ghi.h"

using namespace jupiter;
using namespace jupiter::rendering;

// ============================================================================
// Demo Configuration
// ============================================================================

static const char* WINDOW_TITLE = "Jupiter GHI Primitives Demo";
static constexpr int WINDOW_WIDTH = 1280;
static constexpr int WINDOW_HEIGHT = 720;

// ============================================================================
// Demo State
// ============================================================================

struct DemoState {
    SDL_Window* window = nullptr;
    bool running = true;
    float time = 0.0f;
    
    // GHI backend type
    ghi::Backend backend = ghi::Backend::Vulkan;
    
    // Active pipeline
    ral::Pipeline activePipeline = ral::Pipeline::Simple;
    bool usePBR = false;
    
    // Camera
    glm::vec3 cameraPos = glm::vec3(0.0f, 5.0f, 12.0f);
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);

    float deltaTime = 1.0f / 60.0f;
    
    // Meshes
    ral::MeshHandle cubeMesh;
    ral::MeshHandle sphereMesh;
    ral::MeshHandle planeMesh;
    ral::MeshHandle cylinderMesh;
    ral::MeshHandle capsuleMesh;
    
    // Simple Materials
    ral::MaterialHandle redMaterial;
    ral::MaterialHandle blueMaterial;
    ral::MaterialHandle greenMaterial;
    ral::MaterialHandle grayMaterial;
    ral::MaterialHandle orangeMaterial;
    
    // PBR Materials
    ral::MaterialHandle goldMaterial;
    ral::MaterialHandle silverMaterial;
    ral::MaterialHandle plasticRedMaterial;
    ral::MaterialHandle roughStoneMaterial;
    ral::MaterialHandle smoothMetalMaterial;
};

static DemoState g_demo;
static jupiter::rendering::demo::ImGuiOverlayGHI g_imgui;

// ============================================================================
// Backend Selection
// ============================================================================

ghi::Backend selectBackend(int argc, char* argv[]) {
    // Default backend based on platform
#ifdef __APPLE__
    ghi::Backend selected = ghi::Backend::Metal;  // Native on macOS
#else
    ghi::Backend selected = ghi::Backend::Vulkan; // Best cross-platform option
#endif

    // Parse command line flags (do NOT early-return so flags can be combined)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vulkan") == 0 || strcmp(argv[i], "-v") == 0) {
            selected = ghi::Backend::Vulkan;
            continue;
        }
        if (strcmp(argv[i], "--metal") == 0 || strcmp(argv[i], "-m") == 0) {
            selected = ghi::Backend::Metal;
            continue;
        }
        if (strcmp(argv[i], "--opengl") == 0 || strcmp(argv[i], "-g") == 0) {
            selected = ghi::Backend::OpenGL;
            continue;
        }
        if (strcmp(argv[i], "--pbr") == 0 || strcmp(argv[i], "-p") == 0) {
            g_demo.usePBR = true;
        }
    }

    return selected;
}

// ============================================================================
// Initialization
// ============================================================================

bool initializeSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_ERROR("Demo", "Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    
    // Create window with appropriate flags based on backend
    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;
    
    switch (g_demo.backend) {
        case ghi::Backend::Vulkan:
            windowFlags |= SDL_WINDOW_VULKAN;
            break;
        case ghi::Backend::Metal:
            windowFlags |= SDL_WINDOW_METAL;
            break;
        case ghi::Backend::OpenGL:
            windowFlags |= SDL_WINDOW_OPENGL;
            break;
        default:
            break;
    }
    
    g_demo.window = SDL_CreateWindow(
        WINDOW_TITLE,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        windowFlags
    );
    
    if (!g_demo.window) {
        LOG_ERROR("Demo", "Failed to create window: %s", SDL_GetError());
        return false;
    }
    
    LOG_INFO("Demo", "SDL initialized, window created: %dx%d", WINDOW_WIDTH, WINDOW_HEIGHT);
    return true;
}

bool initializeGHI() {
    LOG_INFO("Demo", "Initializing GHI with backend: %s", ghi::getBackendName(g_demo.backend));
    
    if (!ghi::initialize(g_demo.backend, g_demo.window)) {
        LOG_ERROR("Demo", "Failed to initialize GHI");
        return false;
    }
    
    // Log capabilities
    const ghi::Capabilities& caps = ghi::getCapabilities();
    LOG_INFO("Demo", "GHI Capabilities:");
    LOG_INFO("Demo", "  Max texture size: %u", caps.maxTextureSize);
    LOG_INFO("Demo", "  Compute shaders: %s", caps.hasComputeShaders ? "yes" : "no");
    LOG_INFO("Demo", "  Indirect draw: %s", caps.hasIndirectDraw ? "yes" : "no");
    
    return true;
}

bool initializeRAL() {
    LOG_INFO("Demo", "Initializing RAL...");
    
    if (!ral::initialize()) {
        LOG_ERROR("Demo", "Failed to initialize RAL");
        return false;
    }
    
    // Select pipeline based on command line
    if (g_demo.usePBR && ral::isPipelineAvailable(ral::Pipeline::PBR)) {
        if (ral::usePipeline(ral::Pipeline::PBR)) {
            g_demo.activePipeline = ral::Pipeline::PBR;
            LOG_INFO("Demo", "Using PBR pipeline");
        } else {
            LOG_WARN("Demo", "PBR pipeline selection failed, falling back to Simple");
            ral::usePipeline(ral::Pipeline::Simple);
            g_demo.activePipeline = ral::Pipeline::Simple;
        }
    } else {
        if (!ral::usePipeline(ral::Pipeline::Simple)) {
            LOG_ERROR("Demo", "Failed to select Simple pipeline");
            return false;
        }
        g_demo.activePipeline = ral::Pipeline::Simple;
        LOG_INFO("Demo", "Using Simple pipeline");
    }
    
    return true;
}

bool initializeImGui() {
    // Must be after SDL window exists. Also requires GHI initialized so we can create textures/shaders.
    if (!g_imgui.initialize(g_demo.window, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        LOG_WARN("Demo", "ImGui overlay failed to initialize (continuing without overlay)");
        return false;
    }
    return true;
}

bool createMeshes() {
    LOG_INFO("Demo", "Creating primitive meshes...");
    
    // Create primitives using RAL
    g_demo.cubeMesh = ral::createCube(1.5f);
    if (!g_demo.cubeMesh.isValid()) {
        LOG_ERROR("Demo", "Failed to create cube mesh");
        return false;
    }
    
    g_demo.sphereMesh = ral::createSphere(0.8f, 32);
    if (!g_demo.sphereMesh.isValid()) {
        LOG_ERROR("Demo", "Failed to create sphere mesh");
        return false;
    }
    
    g_demo.planeMesh = ral::createPlane(20.0f, 20.0f, 4, 4);
    if (!g_demo.planeMesh.isValid()) {
        LOG_ERROR("Demo", "Failed to create plane mesh");
        return false;
    }
    
    g_demo.cylinderMesh = ral::createCylinder(0.5f, 2.0f, 32);
    if (!g_demo.cylinderMesh.isValid()) {
        LOG_ERROR("Demo", "Failed to create cylinder mesh");
        return false;
    }
    
    g_demo.capsuleMesh = ral::createCapsule(0.4f, 2.0f, 32);
    if (!g_demo.capsuleMesh.isValid()) {
        LOG_ERROR("Demo", "Failed to create capsule mesh");
        return false;
    }
    
    LOG_INFO("Demo", "All meshes created successfully");
    return true;
}

bool createMaterials() {
    LOG_INFO("Demo", "Creating materials...");
    
    // Simple materials (non-PBR path)
    g_demo.redMaterial = ral::createSimpleMaterial(glm::vec3(0.9f, 0.2f, 0.2f));
    g_demo.blueMaterial = ral::createSimpleMaterial(glm::vec3(0.2f, 0.4f, 0.9f));
    g_demo.greenMaterial = ral::createSimpleMaterial(glm::vec3(0.2f, 0.8f, 0.3f));
    g_demo.grayMaterial = ral::createSimpleMaterial(glm::vec3(0.4f, 0.4f, 0.45f));
    g_demo.orangeMaterial = ral::createSimpleMaterial(glm::vec3(1.0f, 0.5f, 0.1f));
    
    // PBR materials (for PBR pipeline)
    // Gold: metallic, low roughness - mirror-like shine
    g_demo.goldMaterial = ral::createPBRMaterial(
        glm::vec3(1.0f, 0.766f, 0.336f),  // Gold albedo
        1.0f,   // Full metallic
        0.1f    // Very shiny
    );
    
    // Silver: metallic, extremely smooth - chrome-like
    g_demo.silverMaterial = ral::createPBRMaterial(
        glm::vec3(0.972f, 0.960f, 0.915f),  // Silver albedo
        1.0f,   // Full metallic
        0.05f   // Mirror-like
    );
    
    // Red plastic: non-metallic, glossy
    g_demo.plasticRedMaterial = ral::createPBRMaterial(
        glm::vec3(0.9f, 0.1f, 0.1f),  // Red
        0.0f,   // Non-metallic
        0.25f   // Glossy plastic
    );
    
    // Rough stone: non-metallic, very rough (matte)
    g_demo.roughStoneMaterial = ral::createPBRMaterial(
        glm::vec3(0.5f, 0.5f, 0.55f),  // Gray
        0.0f,   // Non-metallic
        0.95f   // Very rough
    );
    
    // Smooth metal: brushed steel look
    g_demo.smoothMetalMaterial = ral::createPBRMaterial(
        glm::vec3(0.5f, 0.5f, 0.6f),  // Steel gray
        0.9f,   // Mostly metallic
        0.35f   // Brushed finish
    );
    
    LOG_INFO("Demo", "Materials created with varying shininess");
    return true;
}

bool setupScene() {
    LOG_INFO("Demo", "Setting up scene...");
    
    // Set camera
    ral::CameraInfo camera;
    camera.position = g_demo.cameraPos;
    camera.target = g_demo.cameraTarget;
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fov = 60.0f;
    camera.aspectRatio = static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    ral::setCamera(camera);
    
    // Set ambient lighting
    ral::setAmbientLight(glm::vec3(0.2f, 0.2f, 0.25f), 0.3f);
    
    // Primary directional light (sun-like)
    ral::createDirectionalLight(
        glm::vec3(0.5f, -0.7f, 0.3f),      // Direction
        glm::vec3(1.0f, 0.95f, 0.85f),     // Warm white
        1.5f                                // Intensity
    );
    
    // Add point lights for PBR mode
    if (g_demo.usePBR) {
        LOG_INFO("Demo", "Adding PBR point lights...");
        
        // Red point light on left
        ral::createPointLight(
            glm::vec3(-5.0f, 3.0f, 2.0f),
            glm::vec3(1.0f, 0.3f, 0.2f),
            15.0f,  // radius
            2.0f    // intensity
        );
        
        // Blue point light on right
        ral::createPointLight(
            glm::vec3(5.0f, 3.0f, 2.0f),
            glm::vec3(0.2f, 0.4f, 1.0f),
            15.0f,  // radius
            2.0f    // intensity
        );
    }
    
    LOG_INFO("Demo", "Scene setup complete");
    return true;
}

// ============================================================================
// Main Loop
// ============================================================================

void processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        g_imgui.processEvent(&event);
        switch (event.type) {
            case SDL_EVENT_QUIT:
                g_demo.running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    g_demo.running = false;
                }
                break;
        }
    }
}

void update(float deltaTime) {
    g_demo.time += deltaTime;
    g_demo.deltaTime = deltaTime;
    
    // Simple camera orbit
    float orbitSpeed = 0.3f;
    float orbitRadius = 12.0f;
    g_demo.cameraPos.x = orbitRadius * std::sin(g_demo.time * orbitSpeed);
    g_demo.cameraPos.z = orbitRadius * std::cos(g_demo.time * orbitSpeed);
    g_demo.cameraPos.y = 5.0f + 2.0f * std::sin(g_demo.time * 0.2f);
    
    // Update camera
    ral::CameraInfo camera;
    camera.position = g_demo.cameraPos;
    camera.target = g_demo.cameraTarget;
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fov = 60.0f;
    camera.aspectRatio = static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    ral::setCamera(camera);
}

void render() {
    // RAL handles all GHI calls (beginFrame, beginRenderPass, etc.)
    ral::beginFrame();

    // ImGui frame (inside the same render pass)
    g_imgui.newFrame(g_demo.deltaTime, WINDOW_WIDTH, WINDOW_HEIGHT);
    
    float t = g_demo.time;
    
    // Select materials based on active pipeline
    bool usePBR = (g_demo.activePipeline == ral::Pipeline::PBR);
    
    ral::MaterialHandle groundMat = usePBR ? g_demo.roughStoneMaterial : g_demo.grayMaterial;
    ral::MaterialHandle cubeMat = usePBR ? g_demo.goldMaterial : g_demo.redMaterial;
    ral::MaterialHandle sphereMat = usePBR ? g_demo.silverMaterial : g_demo.blueMaterial;
    ral::MaterialHandle cylinderMat = usePBR ? g_demo.smoothMetalMaterial : g_demo.greenMaterial;
    ral::MaterialHandle capsuleMat = usePBR ? g_demo.plasticRedMaterial : g_demo.orangeMaterial;
    ral::MaterialHandle orbitMat = usePBR ? g_demo.goldMaterial : g_demo.orangeMaterial;
    
    // Render ground plane
    glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f));
    ral::renderMesh(g_demo.planeMesh, groundTransform, groundMat);
    
    // Render spinning cube (gold in PBR, red in Simple)
    glm::mat4 cubeTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.0f, 0.0f));
    cubeTransform = glm::rotate(cubeTransform, t * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    cubeTransform = glm::rotate(cubeTransform, t * 0.3f, glm::vec3(1.0f, 0.0f, 0.0f));
    ral::renderMesh(g_demo.cubeMesh, cubeTransform, cubeMat);
    
    // Render bouncing sphere (silver in PBR, blue in Simple)
    float bounceY = 1.5f + 0.5f * std::abs(std::sin(t * 2.0f));
    glm::mat4 sphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, bounceY, 0.0f));
    ral::renderMesh(g_demo.sphereMesh, sphereTransform, sphereMat);
    
    // Render rotating cylinder (metallic in PBR, green in Simple)
    glm::mat4 cylinderTransform = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, 0.0f));
    cylinderTransform = glm::rotate(cylinderTransform, t * 0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
    ral::renderMesh(g_demo.cylinderMesh, cylinderTransform, cylinderMat);
    
    // Render swinging capsule (red plastic in PBR, orange in Simple)
    float swingAngle = 0.3f * std::sin(t * 1.5f);
    glm::mat4 capsuleTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 2.0f, 3.0f));
    capsuleTransform = glm::rotate(capsuleTransform, swingAngle, glm::vec3(0.0f, 0.0f, 1.0f));
    ral::renderMesh(g_demo.capsuleMesh, capsuleTransform, capsuleMat);
    
    // Render second sphere (orbiting)
    float orbitAngle = t * 0.8f;
    float orbitR = 5.0f;
    glm::vec3 orbitPos(orbitR * std::cos(orbitAngle), 1.0f, orbitR * std::sin(orbitAngle));
    glm::mat4 orbitSphereTransform = glm::translate(glm::mat4(1.0f), orbitPos);
    orbitSphereTransform = glm::scale(orbitSphereTransform, glm::vec3(0.6f));
    ral::renderMesh(g_demo.sphereMesh, orbitSphereTransform, orbitMat);

    // Overlay UI
    {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360, 160), ImGuiCond_Always);
        if (ImGui::Begin("Primitives Demo (GHI)", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Backend: %s", ghi::getBackendName(g_demo.backend));
            ImGui::Text("Pipeline: %s", (g_demo.activePipeline == ral::Pipeline::PBR) ? "PBR" : "Simple");
            ImGui::Separator();
            ImGui::Text("Flags:");
            ImGui::BulletText("--pbr enables PBR pipeline");
            ImGui::BulletText("JUPITER_DEBUG_PIPELINE=1 draws debug colors");
            ImGui::Separator();
            ImGui::Text("Controls: ESC quits");
        }
        ImGui::End();
    }

    g_imgui.render();
    
    ral::endFrame();  // RAL handles endRenderPass and endFrame
}

// ============================================================================
// Shutdown
// ============================================================================

void shutdown() {
    LOG_INFO("Demo", "Shutting down...");
    
    // Destroy meshes
    if (g_demo.cubeMesh.isValid()) ral::destroyMesh(g_demo.cubeMesh);
    if (g_demo.sphereMesh.isValid()) ral::destroyMesh(g_demo.sphereMesh);
    if (g_demo.planeMesh.isValid()) ral::destroyMesh(g_demo.planeMesh);
    if (g_demo.cylinderMesh.isValid()) ral::destroyMesh(g_demo.cylinderMesh);
    if (g_demo.capsuleMesh.isValid()) ral::destroyMesh(g_demo.capsuleMesh);
    
    // Destroy simple materials
    if (g_demo.redMaterial.isValid()) ral::destroyMaterial(g_demo.redMaterial);
    if (g_demo.blueMaterial.isValid()) ral::destroyMaterial(g_demo.blueMaterial);
    if (g_demo.greenMaterial.isValid()) ral::destroyMaterial(g_demo.greenMaterial);
    if (g_demo.grayMaterial.isValid()) ral::destroyMaterial(g_demo.grayMaterial);
    if (g_demo.orangeMaterial.isValid()) ral::destroyMaterial(g_demo.orangeMaterial);
    
    // Destroy PBR materials
    if (g_demo.goldMaterial.isValid()) ral::destroyMaterial(g_demo.goldMaterial);
    if (g_demo.silverMaterial.isValid()) ral::destroyMaterial(g_demo.silverMaterial);
    if (g_demo.plasticRedMaterial.isValid()) ral::destroyMaterial(g_demo.plasticRedMaterial);
    if (g_demo.roughStoneMaterial.isValid()) ral::destroyMaterial(g_demo.roughStoneMaterial);
    if (g_demo.smoothMetalMaterial.isValid()) ral::destroyMaterial(g_demo.smoothMetalMaterial);
    
    // Shutdown systems in reverse order
    g_imgui.shutdown();
    ral::shutdown();
    ghi::shutdown();
    
    if (g_demo.window) {
        SDL_DestroyWindow(g_demo.window);
        g_demo.window = nullptr;
    }
    
    SDL_Quit();
    
    LOG_INFO("Demo", "Shutdown complete");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    // Initialize logging
    logging::initialize();
    
    LOG_INFO("Demo", "=== Jupiter GHI Primitives Demo ===");
    LOG_INFO("Demo", "Usage: %s [--vulkan|-v] [--metal|-m] [--opengl|-g] [--pbr|-p]", argv[0]);
    LOG_INFO("Demo", "  --vulkan, -v  : Use Vulkan backend");
    LOG_INFO("Demo", "  --metal, -m   : Use Metal backend (macOS only)");
    LOG_INFO("Demo", "  --opengl, -g  : Use OpenGL backend (fallback)");
    LOG_INFO("Demo", "  --pbr, -p     : Use PBR pipeline (default: Simple)");
    
    // Select backend
    g_demo.backend = selectBackend(argc, argv);
    LOG_INFO("Demo", "Selected backend: %s", ghi::getBackendName(g_demo.backend));
    
    // Initialize SDL
    if (!initializeSDL()) {
        return 1;
    }
    
    // Initialize GHI
    if (!initializeGHI()) {
        shutdown();
        return 1;
    }
    
    // Initialize RAL
    if (!initializeRAL()) {
        shutdown();
        return 1;
    }

    // Initialize ImGui overlay (optional)
    initializeImGui();
    
    // Create resources
    if (!createMeshes() || !createMaterials() || !setupScene()) {
        shutdown();
        return 1;
    }
    
    LOG_INFO("Demo", "=== Starting main loop ===");
    LOG_INFO("Demo", "Active pipeline: %s", 
             g_demo.activePipeline == ral::Pipeline::PBR ? "PBR (Cook-Torrance)" : "Simple (Lambertian)");
    LOG_INFO("Demo", "Controls:");
    LOG_INFO("Demo", "  ESC - Exit");
    
    // Main loop
    Uint64 lastTime = SDL_GetTicks();
    
    while (g_demo.running) {
        Uint64 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        
        processEvents();
        update(deltaTime);
        render();
    }
    
    shutdown();
    
    LOG_INFO("Demo", "=== Demo completed ===");
    return 0;
}

