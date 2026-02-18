/**
 * @file main.cpp
 * @brief Landscape Demo - Grassy outdoor terrain with player trails
 * 
 * Demonstrates:
 * - Procedural heightmap terrain generation
 * - GPU-generated grass (compute shader instancing)
 * - Player trails (flatten + bend grass along paths)
 * - Trail relaxation system (5-20s programmable)
 * - Wind animation
 * 
 * Controls:
 * - WASD: Move (creates trails)
 * - Mouse: Look
 * - Space/C: Up/Down
 * - G: Toggle grass
 * - T: Toggle trail debug view
 * - [/]: Grass radius
 * - -/=: Grass density
 * - ,/.: Trail relax time
 */

#include "rendering/application.h"
#include "rendering/lighting.h"
#include "rendering/vulkan_compute_pipeline.h"
#include "terrain_heightmap.h"
#include "trail_field.h"
#include "grass_system.h"
#include "logging/logging.h"
#include "input/input.h"
#include "math/math.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>

using namespace jupiter::rendering::vulkan;

using namespace jupiter;
using namespace jupiter::rendering;
using namespace landscape;

/**
 * @brief Simple fly camera for landscape exploration
 */
class FlyCamera {
public:
    FlyCamera() = default;

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
        if (keyState[SDL_SCANCODE_LSHIFT]) velocity *= 3.0f;  // Sprint
        if (keyState[SDL_SCANCODE_LCTRL]) velocity *= 0.25f;  // Slow

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

        // Clamp pitch
        if (pitch_ > glm::radians(89.0f)) pitch_ = glm::radians(89.0f);
        if (pitch_ < glm::radians(-89.0f)) pitch_ = glm::radians(-89.0f);
    }

    void setMoveSpeed(float speed) { moveSpeed_ = speed; }

private:
    glm::vec3 position_ = glm::vec3(0.0f);
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float moveSpeed_ = 10.0f;
    float mouseSensitivity_ = 0.002f;
};

/**
 * @brief Landscape demo application
 */
class LandscapeDemo : public Application {
public:
    LandscapeDemo()
        : Application("Jupiter Engine - Landscape Demo", 1024, 768, false) {}

protected:
    void onInit() override {
        std::cout << "\n\n========== LANDSCAPE DEMO ON INIT CALLED ==========\n\n" << std::flush;
        LOG_INFO("LandscapeDemo", "=== Landscape Demo Starting ===");
        
        // Initialize input
        input::InputManager::get().initialize();
        
        // Enable PBR auto-render (now works with MoltenVK fix!)
        std::cout << "Enabling PBR auto-render...\n" << std::flush;
        enableAutoRender();
        std::cout << "PBR auto-render enabled\n" << std::flush;
        
        // Create camera
        camera_ = createPerspectiveCamera(math::PI / 3.0f, 0.0f, 0.1f, 2000.0f);
        setActiveCamera(camera_);
        
        // Position camera close to see the big cube
        flyCamera_.setPosition(glm::vec3(0.0f, 0.0f, 50.0f));  // 50m in front of cube
        flyCamera_.setYawPitch(glm::radians(180.0f), glm::radians(0.0f));  // Look back at cube
        flyCamera_.setMoveSpeed(15.0f);
        
        std::cout << "Camera at (0, 0, 50) looking at origin - should see 20m red cube\n" << std::flush;
        
        // Setup lighting
        float sunDir[3] = {-0.4f, -0.8f, -0.3f};
        float sunColor[3] = {1.0f, 0.95f, 0.85f};
        addDirectionalLight(sunDir, sunColor, 3.0f);
        
        float ambientColor[3] = {0.4f, 0.45f, 0.5f};
        setAmbientLight(ambientColor, 1.2f);
        
        // Spawn reference cubes
        std::cout << "Spawning reference cubes...\n" << std::flush;
        
        float cube1Pos[3] = {0.0f, 10.0f, 0.0f};
        float cube1Color[3] = {1.0f, 0.0f, 0.0f};  // Red
        spawnCube(cube1Pos, 10.0f, cube1Color);
        
        float cube2Pos[3] = {100.0f, 10.0f, 0.0f};
        float cube2Color[3] = {0.0f, 1.0f, 1.0f};  // Cyan
        spawnCube(cube2Pos, 10.0f, cube2Color);
        
        float cube3Pos[3] = {-100.0f, 10.0f, 0.0f};
        float cube3Color[3] = {1.0f, 1.0f, 0.0f};  // Yellow
        spawnCube(cube3Pos, 10.0f, cube3Color);
        
        std::cout << "Reference cubes spawned\n" << std::flush;
        
        // Generate terrain
        std::cout << "=== Generating terrain ===\n" << std::flush;
        terrain_ = std::make_unique<TerrainHeightmap>();
        std::cout << "TerrainHeightmap object created\n" << std::flush;
        
        TerrainConfig terrainCfg;
        terrainCfg.size = 1024.0f;
        terrainCfg.segments = 256;
        terrainCfg.heightScale = 50.0f;
        terrainCfg.textureRes = 512;
        
        std::cout << "Calling terrain->generate()...\n" << std::flush;
        bool terrainOk = terrain_->generate(getVulkanDevice(),
                                            getVulkanAllocator(),
                                            getVulkanCommandPool(),
                                            getVulkanGraphicsQueue(),
                                            terrainCfg);
        std::cout << "terrain->generate() returned: " << terrainOk << "\n" << std::flush;
        
        if (!terrainOk) {
            std::cout << "ERROR: Failed to generate terrain!\n" << std::flush;
            return;
        }
        
        // Add terrain to scene (PBR now works!)
        std::cout << "Adding terrain to scene...\n" << std::flush;
        terrainHandle_ = terrain_->addToScene(getSceneManager(), 
                                              getMaterialSystem(),
                                              getVulkanAllocator());
        
        if (!terrainHandle_.isValid()) {
            std::cout << "ERROR: Failed to add terrain!\n" << std::flush;
        } else {
            std::cout << "Terrain added successfully\n" << std::flush;
        }
        
        std::cout << "=== SKIPPING grass/trail for debugging (just showing cube + terrain) ===\n" << std::flush;
        
        // TODO: Re-enable once basic rendering works
        /*
        // Initialize trail field
        std::cout << "=== Initializing trail field ===\n" << std::flush;
        trailField_ = std::make_unique<TrailField>();
        if (!trailField_->initialize(getVulkanDevice(), getVulkanAllocator(),
                                      getVulkanCommandPool(), getVulkanGraphicsQueue(),
                                      256.0f, 512)) {
            std::cout << "ERROR: Failed to initialize trail field\n" << std::flush;
            return;
        }
        
        // Initialize grass system
        std::cout << "=== Initializing grass system ===\n" << std::flush;
        grassSystem_ = std::make_unique<GrassSystem>();
        if (!grassSystem_->initialize(getVulkanDevice(), getVulkanPhysicalDevice(),
                                       getVulkanAllocator(), getVulkanRenderPass(),
                                       getRenderGlobals())) {
            std::cout << "ERROR: Failed to initialize grass system\n" << std::flush;
            return;
        }
        
        grassSystem_->bindHeightmap(terrain_->getHeightmapView(),
                                     terrain_->getHeightmapSampler(),
                                     terrain_->getSize());
        grassSystem_->bindTrailField(trailField_.get());
        grassSystem_->setWind(glm::vec3(1.0f, 0.0f, 0.3f), 0.5f, 0.3f);
        */
        
        // Capture mouse (SDL3 uses input manager)
        input::InputManager::get().setMouseCaptured(true);
        mouseCaptured_ = true;
        
        printControls();
        LOG_INFO("LandscapeDemo", "=== Initialization Complete ===");
    }

    void printControls() {
        std::cout << R"(
╔═══════════════════════════════════════════════════════════════════╗
║                    LANDSCAPE DEMO CONTROLS                        ║
╠═══════════════════════════════════════════════════════════════════╣
║  MOVEMENT (creates trails!)                                       ║
║    W/S        - Move forward/backward                             ║
║    A/D        - Strafe left/right                                 ║
║    SPACE      - Move up                                           ║
║    C          - Move down                                         ║
║    SHIFT      - Sprint (3x speed)                                 ║
║    CTRL       - Slow walk (0.25x speed)                           ║
║    Mouse      - Look around                                       ║
║                                                                   ║
║  GRASS CONTROLS                                                   ║
║    G          - Toggle grass rendering                            ║
║    T          - Toggle trail debug view                           ║
║    [ / ]      - Decrease/Increase grass radius                    ║
║    - / =      - Decrease/Increase grass density                   ║
║    , / .      - Decrease/Increase trail relax time (5-20s)        ║
║                                                                   ║
║  OTHER                                                            ║
║    F          - Print status                                      ║
║    TAB        - Toggle mouse capture                              ║
║    ESC        - Exit                                              ║
║                                                                   ║
╚═══════════════════════════════════════════════════════════════════╝
)" << std::endl;
    }

    void onInput(float deltaTime) override {
        input::InputManager::get().update();
        
        // Poll SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                requestClose();
                return;
            }
            
            if (event.type == SDL_EVENT_KEY_DOWN) {
                handleKeyPress(event.key.scancode);
            }
            
            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured_) {
                flyCamera_.processMouse(event.motion.xrel, event.motion.yrel);
            }
        }
    }

    void handleKeyPress(SDL_Scancode key) {
        switch (key) {
            case SDL_SCANCODE_ESCAPE:
                requestClose();
                break;
            
            case SDL_SCANCODE_TAB:
                mouseCaptured_ = !mouseCaptured_;
                input::InputManager::get().setMouseCaptured(mouseCaptured_);
                LOG_INFO("LandscapeDemo", "Mouse %s", mouseCaptured_ ? "captured" : "released");
                break;
            
            case SDL_SCANCODE_G:
                if (grassSystem_) {
                    grassEnabled_ = !grassEnabled_;
                    grassSystem_->setEnabled(grassEnabled_);
                    LOG_INFO("LandscapeDemo", "Grass: %s", grassEnabled_ ? "ON" : "OFF");
                }
                break;
            
            case SDL_SCANCODE_T:
                trailDebug_ = !trailDebug_;
                LOG_INFO("LandscapeDemo", "Trail debug: %s", trailDebug_ ? "ON" : "OFF");
                break;
            
            case SDL_SCANCODE_LEFTBRACKET:
                if (grassSystem_) {
                    auto& params = grassSystem_->getParams();
                    params.grassRadius = std::max(32.0f, params.grassRadius - 16.0f);
                    LOG_INFO("LandscapeDemo", "Grass radius: %.0fm", params.grassRadius);
                }
                break;
            
            case SDL_SCANCODE_RIGHTBRACKET:
                if (grassSystem_) {
                    auto& params = grassSystem_->getParams();
                    params.grassRadius = std::min(256.0f, params.grassRadius + 16.0f);
                    LOG_INFO("LandscapeDemo", "Grass radius: %.0fm", params.grassRadius);
                }
                break;
            
            case SDL_SCANCODE_MINUS:
                if (grassSystem_) {
                    auto& params = grassSystem_->getParams();
                    params.densityMul = std::max(0.1f, params.densityMul - 0.1f);
                    LOG_INFO("LandscapeDemo", "Grass density: %.1fx", params.densityMul);
                }
                break;
            
            case SDL_SCANCODE_EQUALS:
                if (grassSystem_) {
                    auto& params = grassSystem_->getParams();
                    params.densityMul = std::min(3.0f, params.densityMul + 0.1f);
                    LOG_INFO("LandscapeDemo", "Grass density: %.1fx", params.densityMul);
                }
                break;
            
            case SDL_SCANCODE_COMMA:
                if (trailField_) {
                    trailRelaxSeconds_ = std::max(5.0f, trailRelaxSeconds_ - 1.0f);
                    trailField_->setRelaxSeconds(trailRelaxSeconds_);
                    LOG_INFO("LandscapeDemo", "Trail relax: %.1fs", trailRelaxSeconds_);
                }
                break;
            
            case SDL_SCANCODE_PERIOD:
                if (trailField_) {
                    trailRelaxSeconds_ = std::min(20.0f, trailRelaxSeconds_ + 1.0f);
                    trailField_->setRelaxSeconds(trailRelaxSeconds_);
                    LOG_INFO("LandscapeDemo", "Trail relax: %.1fs", trailRelaxSeconds_);
                }
                break;
            
            case SDL_SCANCODE_F:
                printStatus();
                break;
            
            default:
                break;
        }
    }

    void onUpdate(float deltaTime) override {
        totalTime_ += deltaTime;
        deltaTime_ = deltaTime;
        frameCount_++;
        fpsAccumulator_ += deltaTime;
        
        if (fpsAccumulator_ >= 1.0f) {
            currentFPS_ = static_cast<float>(frameCount_) / fpsAccumulator_;
            frameCount_ = 0;
            fpsAccumulator_ = 0.0f;
        }
        
        // Update camera from input
        const bool* keyState = SDL_GetKeyboardState(nullptr);
        if (mouseCaptured_) {
            flyCamera_.processInput(keyState, deltaTime);
        }
        
        // Update camera transform
        glm::vec3 pos = flyCamera_.getPosition();
        glm::vec3 target = pos + flyCamera_.getForward();
        camera_->setPosition({pos.x, pos.y, pos.z});
        camera_->setTarget({target.x, target.y, target.z});
        
        // Clamp camera to terrain (optional, for nicer demo)
        if (terrain_) {
            float groundHeight = terrain_->sampleHeight(pos.x, pos.z);
            if (pos.y < groundHeight + 2.0f) {
                pos.y = groundHeight + 2.0f;
                flyCamera_.setPosition(pos);
            }
        }
    }

    void onPreRenderPass(VkCommandBuffer cmd, uint32_t frameIndex) override {
        // Temporarily disabled for debugging
        return;
        
        if (!trailField_ || !grassSystem_) return;
        
        // Store cmd for use in onRender
        currentCmd_ = cmd;
        
        // Generate player trail event
        glm::vec3 currentPos = flyCamera_.getPosition();
        glm::vec3 velocity = (currentPos - prevPlayerPos_) / std::max(deltaTime_, 0.001f);
        float speed = glm::length(velocity);
        
        if (speed > 0.1f) {  // Only create trails when moving
            TrailEvent event;
            event.pos_radius = glm::vec4(currentPos.x, currentPos.z, 1.5f, 1.0f);
            
            glm::vec2 dir2D = glm::normalize(glm::vec2(velocity.x, velocity.z));
            event.dir_bend = glm::vec4(dir2D, 0.8f, 0.9f);  // bendStrength, flattenStrength
            
            trailField_->pushEvent(event);
        }
        
        prevPlayerPos_ = currentPos;
        
        // Update trail field (center on player)
        glm::vec2 trailOrigin = glm::vec2(currentPos.x, currentPos.z) - glm::vec2(128.0f);
        trailField_->update(cmd, deltaTime_, trailOrigin);
        
        // Barrier: trail writes → grass reads
        VulkanComputePipeline::barrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        
        // Reset grass counters
        grassSystem_->resetCounters(cmd);
        
        // Generate grass instances
        grassSystem_->generateInstances(cmd, currentPos, deltaTime_);
        
        // Barrier: grass compute writes → indirect draw + vertex reads
        VulkanComputePipeline::barrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
    }

    void onRender() override {
        // PBR auto-render handles cubes and terrain automatically
        // Just draw grass if enabled
        if (grassSystem_ && grassEnabled_ && currentCmd_ != VK_NULL_HANDLE) {
            grassSystem_->draw(currentCmd_, getCurrentFrameIndex());
        }
    }

    void printStatus() {
        glm::vec3 pos = flyCamera_.getPosition();
        std::cout << "\n=== Status ===\n";
        std::cout << "FPS: " << currentFPS_ << "\n";
        std::cout << "Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        
        if (grassSystem_) {
            const auto& params = grassSystem_->getParams();
            std::cout << "Grass:\n";
            std::cout << "  Enabled: " << (grassEnabled_ ? "Yes" : "No") << "\n";
            std::cout << "  Radius: " << params.grassRadius << "m\n";
            std::cout << "  Density: " << params.densityMul << "x\n";
            std::cout << "  Cell size: " << params.cellSize << "m\n";
        }
        
        if (trailField_) {
            std::cout << "Trails:\n";
            std::cout << "  Relax time: " << trailRelaxSeconds_ << "s\n";
            std::cout << "  Debug view: " << (trailDebug_ ? "On" : "Off") << "\n";
        }
    }

    void onShutdown() override {
        LOG_INFO("LandscapeDemo", "Shutting down");
        
        // Destroy grass system before terrain (grass references heightmap)
        grassSystem_.reset();
        trailField_.reset();
        terrain_.reset();
        
        input::InputManager::get().shutdown();
    }

private:
    // Camera
    FlyCamera flyCamera_;
    PerspectiveCamera* camera_ = nullptr;
    bool mouseCaptured_ = true;
    
    // Systems
    std::unique_ptr<TerrainHeightmap> terrain_;
    std::unique_ptr<TrailField> trailField_;
    std::unique_ptr<GrassSystem> grassSystem_;
    RenderableHandle terrainHandle_;
    VulkanMesh* terrainMesh_ = nullptr;
    
    // State
    bool grassEnabled_ = true;
    bool trailDebug_ = false;
    float trailRelaxSeconds_ = 12.0f;
    
    // Timing
    float totalTime_ = 0.0f;
    float deltaTime_ = 0.0f;
    float currentFPS_ = 0.0f;
    float fpsAccumulator_ = 0.0f;
    int frameCount_ = 0;
    
    // Trail tracking
    glm::vec3 prevPlayerPos_ = glm::vec3(0.0f, 50.0f, 100.0f);
    
    // Command buffer (for onRender, since getRenderer() returns incomplete type)
    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE;
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    logging::initialize();
    LOG_INFO("Main", "Starting Landscape Demo");

    std::cout << R"(
╔══════════════════════════════════════════════════════════════════╗
║                                                                  ║
║       ██╗██╗   ██╗██████╗ ██╗████████╗███████╗██████╗           ║
║       ██║██║   ██║██╔══██╗██║╚══██╔══╝██╔════╝██╔══██╗          ║
║       ██║██║   ██║██████╔╝██║   ██║   █████╗  ██████╔╝          ║
║  ██   ██║██║   ██║██╔═══╝ ██║   ██║   ██╔══╝  ██╔══██╗          ║
║  ╚█████╔╝╚██████╔╝██║     ██║   ██║   ███████╗██║  ██║          ║
║   ╚════╝  ╚═════╝ ╚═╝     ╚═╝   ╚═╝   ╚══════╝╚═╝  ╚═╝          ║
║                                                                  ║
║              LANDSCAPE DEMO - GRASSY OUTDOORS                    ║
║                                                                  ║
║  Features:                                                       ║
║    • Procedural heightmap terrain (1km²)                        ║
║    • GPU-generated grass (compute shader)                       ║
║    • Player trails (flatten + bend)                             ║
║    • Trail relaxation (5-20s programmable)                      ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
)" << std::endl;

    LandscapeDemo demo;
    int result = demo.run();

    LOG_INFO("Main", "Demo exited with code %d", result);
    return result;
}

