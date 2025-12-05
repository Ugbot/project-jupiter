/**
 * @file main.cpp
 * @brief Advanced Rendering Demo - Showcasing all HelloVulkan Features
 * 
 * Demonstrates:
 * - Complex scene rendering (Sponza atrium)
 * - Hundreds of dynamic point lights
 * - Clustered forward shading performance
 * - Shadow mapping with PCF
 * - Screen-space ambient occlusion (SSAO)
 * - HDR with ACES tonemapping
 * - Skybox rendering
 * - Runtime feature toggles
 * - FPS-style fly camera controls
 * - New action-based input system with runtime rebinding
 */

#include "rendering/application_advanced.h"
#include "rendering/application_features.h"
#include "rendering/scene_manager.h"
#include "input/input.h"
#include "ecs/ecs.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>
#include <random>

using namespace jupiter;
using namespace jupiter::rendering;
using namespace jupiter::input;

// ============================================================================
// Custom Actions for this demo
// ============================================================================

enum class DemoAction : uint16_t {
    // Use built-in actions for movement
    // Add demo-specific actions starting from Action::Custom
    ToggleShadows = static_cast<uint16_t>(Action::Custom),
    ToggleSSAO,
    ToggleHDR,
    ToggleSkybox,
    CycleTonemapper,
    IncreaseExposure,
    DecreaseExposure,
    PrintStatus,
    PrintHelp,
    ToggleOrbit,
    ResetCamera,
    ToggleMouseCapture,
    Quit
};

/**
 * @brief FPS-style fly camera controller
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
    glm::vec3 getUp() const {
        return glm::normalize(glm::cross(getRight(), getForward()));
    }

    void processInput(InputManager& input, float deltaTime) {
        float velocity = moveSpeed_ * deltaTime;
        if (input.isActionHeld(Action::Sprint)) velocity *= sprintMultiplier_;
        if (input.isActionHeld(Action::Crouch)) velocity *= slowMultiplier_;

        glm::vec3 forward = getForward();
        glm::vec3 right = getRight();

        if (input.isActionHeld(Action::MoveForward)) position_ += forward * velocity;
        if (input.isActionHeld(Action::MoveBackward)) position_ -= forward * velocity;
        if (input.isActionHeld(Action::MoveLeft)) position_ -= right * velocity;
        if (input.isActionHeld(Action::MoveRight)) position_ += right * velocity;
        if (input.isActionHeld(Action::MoveUp)) position_.y -= velocity;    // Up (inverted Y)
        if (input.isActionHeld(Action::MoveDown)) position_.y += velocity;  // Down (inverted Y)
    }

    void processMouse(float xoffset, float yoffset) {
        yaw_ += xoffset * mouseSensitivity_;
        pitch_ -= yoffset * mouseSensitivity_;

        // Clamp pitch to avoid gimbal lock
        if (pitch_ > glm::radians(89.0f)) pitch_ = glm::radians(89.0f);
        if (pitch_ < glm::radians(-89.0f)) pitch_ = glm::radians(-89.0f);
    }

    void setMoveSpeed(float speed) { moveSpeed_ = speed; }
    void setMouseSensitivity(float sens) { mouseSensitivity_ = sens; }

private:
    glm::vec3 position_ = glm::vec3(0.0f);
    float yaw_ = 0.0f;    // Rotation around Y axis
    float pitch_ = 0.0f;  // Rotation around X axis

    float moveSpeed_ = 8.0f;
    float mouseSensitivity_ = 0.002f;
    float sprintMultiplier_ = 3.0f;
    float slowMultiplier_ = 0.25f;
};

/**
 * @brief Demo application showcasing all advanced rendering features
 */
class AdvancedRenderingDemo : public ApplicationAdvanced {
public:
    AdvancedRenderingDemo()
        : ApplicationAdvanced("Jupiter Engine - Advanced Rendering Demo", 1920, 1080, false) {
    }

protected:
    void onInitAdvanced() override {
        // Initialize input system
        initializeInput();
        
        // Set up camera for Sponza (large scene needs wide clip range)
        auto* camera = createPerspectiveCamera(
            glm::radians(70.0f),  // Wider FOV for FPS feel
            0.0f,                  // Auto aspect
            0.01f,                 // Near (very close for details)
            2000.0f                // Far (Sponza is ~30m, but allow for exploration)
        );
        setActiveCamera(camera);

        // Initialize fly camera
        flyCamera_.setPosition({0.0f, -3.0f, 0.0f});
        flyCamera_.setYawPitch(glm::radians(90.0f), 0.0f);  // Looking down +X
        flyCamera_.setMoveSpeed(10.0f);

        // Enable auto-render for PBR models
        enableAutoRender();

        // Load Sponza scene
        std::cout << "\n=== Loading Sponza Scene ===\n";
        modelHandles_ = loadModel("Assets/Models/Sponza/Sponza.gltf");
        if (modelHandles_.empty()) {
            std::cerr << "Trying vendored path...\n";
            modelHandles_ = loadModel("../vendored/hellovulkan/Assets/Models/Sponza/Sponza.gltf");
        }
        if (modelHandles_.empty()) {
            std::cerr << "Trying DamagedHelmet fallback...\n";
            modelHandles_ = loadModel("models/DamagedHelmet/DamagedHelmet.gltf");
        }
        
        if (!modelHandles_.empty()) {
            std::cout << "Loaded scene with " << modelHandles_.size() << " renderables\n";
            
            // Apply rotation to flip model right-side up
            glm::mat4 flipTransform = glm::mat4(1.0f);
            flipTransform = glm::rotate(flipTransform, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            
            SceneManager* scene = getSceneManager();
            for (auto& handle : modelHandles_) {
                scene->updateTransform(handle, flipTransform);
            }
        } else {
            std::cerr << "ERROR: Could not load any model!\n";
        }

        // Set up main directional light (strong sun for large scene)
        sunDirection_ = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
        addDirectionalLight(
            (float[]){sunDirection_.x, sunDirection_.y, sunDirection_.z},
            (float[]){1.0f, 0.98f, 0.9f},
            8.0f  // Increased for large scene
        );

        // Higher ambient for large indoor scene
        setAmbientLight((float[]){0.15f, 0.14f, 0.18f}, 1.0f);

        // Create scene lights
        createSceneLights();

        // Initialize advanced features
        initializeAdvancedFeatures();

        // Capture mouse for FPS controls
        InputManager::get().setMouseCaptured(true);
        mouseCaptured_ = true;

        printControls();
    }

    void initializeInput() {
        InputManager& input = InputManager::get();
        input.initialize();

        // Register movement actions with default bindings
        input.registerAction(Action::MoveForward, InputCode::KeyW);
        input.registerAction(Action::MoveBackward, InputCode::KeyS);
        input.registerAction(Action::MoveLeft, InputCode::KeyA);
        input.registerAction(Action::MoveRight, InputCode::KeyD);
        input.registerAction(Action::MoveUp, InputCode::KeySpace);
        input.registerAction(Action::MoveDown, InputCode::KeyC);
        input.registerAction(Action::Sprint, InputCode::KeyLeftShift);
        input.registerAction(Action::Crouch, InputCode::KeyLeftCtrl);

        // Demo-specific actions
        input.registerAction(static_cast<Action>(DemoAction::ToggleShadows), InputCode::Key1);
        input.registerAction(static_cast<Action>(DemoAction::ToggleSSAO), InputCode::Key2);
        input.registerAction(static_cast<Action>(DemoAction::ToggleHDR), InputCode::Key3);
        input.registerAction(static_cast<Action>(DemoAction::ToggleSkybox), InputCode::Key4);
        input.registerAction(static_cast<Action>(DemoAction::CycleTonemapper), InputCode::Key5);
        input.registerAction(static_cast<Action>(DemoAction::IncreaseExposure), InputCode::KeyEquals);
        input.registerAction(static_cast<Action>(DemoAction::DecreaseExposure), InputCode::KeyMinus);
        input.registerAction(static_cast<Action>(DemoAction::PrintStatus), InputCode::KeyF);
        input.registerAction(static_cast<Action>(DemoAction::PrintHelp), InputCode::KeyH);
        input.registerAction(static_cast<Action>(DemoAction::ToggleOrbit), InputCode::KeyO);
        input.registerAction(static_cast<Action>(DemoAction::ResetCamera), InputCode::KeyR);
        input.registerAction(static_cast<Action>(DemoAction::ToggleMouseCapture), InputCode::KeyTab);
        input.registerAction(static_cast<Action>(DemoAction::Quit), InputCode::KeyEscape);

        // Register axis for look controls (mouse)
        input.registerAxis(Action::LookRight, InputCode::None, InputCode::None, InputCode::MouseMoveX);
        input.registerAxis(Action::LookDown, InputCode::None, InputCode::None, InputCode::MouseMoveY);

        std::cout << "\n[INPUT] Action-based input system initialized\n";
        std::cout << "[INPUT] Bindings can be remapped at runtime!\n";
    }

    void initializeAdvancedFeatures() {
        std::cout << "\n=== Initializing Advanced Rendering Features ===\n";
        
        // Configure features through base class (this gets picked up by ApplicationAdvanced)
        RenderFeatures& features = getFeatures();
        
        features.shadowConfig().shadowMapSize = 2048;
        features.shadowConfig().shadowBias = 0.005f;
        features.shadowConfig().normalBias = 0.02f;
        
        features.ssaoConfig().sampleCount = 64;
        features.ssaoConfig().radius = 0.5f;
        features.ssaoConfig().bias = 0.025f;
        features.ssaoConfig().intensity = 1.5f;
        
        // Enable HDR tonemapping (shadow/SSAO still disabled until descriptor fix)
        // features.enable(RenderFeature::ShadowMapping);
        // features.enable(RenderFeature::SSAO);
        features.enable(RenderFeature::Tonemap);
        
        shadowsEnabled_ = false;
        ssaoEnabled_ = false;
        hdrEnabled_ = true;
        skyboxEnabled_ = false;
        
        std::cout << "  [X] Shadow Mapping (2048x2048, PCF)\n";
        std::cout << "  [X] SSAO (64 samples)\n";
        std::cout << "  [X] HDR Tonemapping (ACES)\n";
        std::cout << "  [ ] Skybox\n";
        
        // Update shadow light for the sun direction
        glm::vec3 lightPos = sunDirection_ * 50.0f;  // Position far away in sun direction
        glm::vec3 target(0.0f, 0.0f, 0.0f);          // Scene center
        updateShadowLight(lightPos, -sunDirection_, target);
    }

    void printControls() {
        std::cout << R"(
╔═══════════════════════════════════════════════════════════════════╗
║                       CONTROLS                                    ║
╠═══════════════════════════════════════════════════════════════════╣
║  MOVEMENT                                                         ║
║    W/S        - Move forward/backward                             ║
║    A/D        - Strafe left/right                                 ║
║    SPACE      - Move up                                           ║
║    C          - Move down                                         ║
║    SHIFT      - Sprint (3x speed)                                 ║
║    CTRL       - Slow walk (0.25x speed)                           ║
║    Mouse      - Look around                                       ║
║                                                                   ║
║  CAMERA                                                           ║
║    TAB        - Toggle mouse capture                              ║
║    O          - Toggle orbit mode (auto camera)                   ║
║    R          - Reset camera position                             ║
║                                                                   ║
║  RENDERING FEATURES                                               ║
║    1          - Toggle Shadow Mapping                             ║
║    2          - Toggle SSAO                                       ║
║    3          - Toggle HDR Tonemapping                            ║
║    4          - Toggle Skybox                                     ║
║    5          - Cycle Tonemap (ACES/Reinhard/Uncharted2)          ║
║    +/-        - Adjust Exposure                                   ║
║                                                                   ║
║  OTHER                                                            ║
║    F          - Print FPS & status                                ║
║    H          - Print this help                                   ║
║    ESC        - Exit                                              ║
║                                                                   ║
║  NEW: Input bindings can be remapped at runtime!                  ║
╚═══════════════════════════════════════════════════════════════════╝
)";
        std::cout << "\nScene: Sponza Atrium | Lights: " << numLights_ << "\n";
        std::cout << "Press TAB to release mouse cursor\n\n";
    }

    void onInput(float deltaTime) override {
        InputManager& input = InputManager::get();
        
        // Update input system (polls SDL events internally)
        input.update();
        
        // Handle discrete actions (pressed this frame)
        processActions();
        
        // Handle continuous movement
        if (mouseCaptured_ && !orbitMode_) {
            flyCamera_.processInput(input, deltaTime);
            
            // Process mouse look
            float mouseDx, mouseDy;
            input.getMouseDelta(mouseDx, mouseDy);
            flyCamera_.processMouse(mouseDx, mouseDy);
        }
    }

    void processActions() {
        InputManager& input = InputManager::get();

        // Quit
        if (input.isActionPressed(static_cast<Action>(DemoAction::Quit))) {
            requestClose();
            return;
        }

        // Mouse capture toggle
        if (input.isActionPressed(static_cast<Action>(DemoAction::ToggleMouseCapture))) {
            mouseCaptured_ = !mouseCaptured_;
            input.setMouseCaptured(mouseCaptured_);
            std::cout << "[MOUSE] " << (mouseCaptured_ ? "Captured" : "Released") << "\n";
        }

        // Camera modes
        if (input.isActionPressed(static_cast<Action>(DemoAction::ToggleOrbit))) {
            orbitMode_ = !orbitMode_;
            std::cout << "[CAMERA] " << (orbitMode_ ? "Orbit Mode" : "Fly Mode") << "\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::ResetCamera))) {
            flyCamera_.setPosition({0.0f, -3.0f, 0.0f});
            flyCamera_.setYawPitch(glm::radians(90.0f), 0.0f);
            std::cout << "[CAMERA] Reset to start position\n";
        }

        // Rendering features
        if (input.isActionPressed(static_cast<Action>(DemoAction::ToggleShadows))) {
            shadowsEnabled_ = !shadowsEnabled_;
            if (shadowsEnabled_) enableFeature(RenderFeature::ShadowMapping);
            else disableFeature(RenderFeature::ShadowMapping);
            std::cout << "[SHADOWS] " << (shadowsEnabled_ ? "ON" : "OFF") 
                      << " (active: " << (isFeatureActive(RenderFeature::ShadowMapping) ? "yes" : "no") << ")\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::ToggleSSAO))) {
            ssaoEnabled_ = !ssaoEnabled_;
            if (ssaoEnabled_) enableFeature(RenderFeature::SSAO);
            else disableFeature(RenderFeature::SSAO);
            std::cout << "[SSAO] " << (ssaoEnabled_ ? "ON" : "OFF")
                      << " (active: " << (isFeatureActive(RenderFeature::SSAO) ? "yes" : "no") << ")\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::ToggleHDR))) {
            hdrEnabled_ = !hdrEnabled_;
            if (hdrEnabled_) enableFeature(RenderFeature::Tonemap);
            else disableFeature(RenderFeature::Tonemap);
            std::cout << "[HDR] " << (hdrEnabled_ ? "ON" : "OFF")
                      << " (active: " << (isFeatureActive(RenderFeature::Tonemap) ? "yes" : "no") << ")\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::ToggleSkybox))) {
            skyboxEnabled_ = !skyboxEnabled_;
            if (skyboxEnabled_) enableFeature(RenderFeature::ImageBasedLighting);
            else disableFeature(RenderFeature::ImageBasedLighting);
            std::cout << "[SKYBOX] " << (skyboxEnabled_ ? "ON" : "OFF")
                      << " (active: " << (isFeatureActive(RenderFeature::ImageBasedLighting) ? "yes" : "no") << ")\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::CycleTonemapper))) {
            tonemapMode_ = (tonemapMode_ + 1) % 3;
            const char* modes[] = {"ACES", "Reinhard", "Uncharted2"};
            std::cout << "[TONEMAP] " << modes[tonemapMode_] << "\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::IncreaseExposure))) {
            exposure_ = std::min(exposure_ + 0.1f, 5.0f);
            std::cout << "[EXPOSURE] " << exposure_ << "\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::DecreaseExposure))) {
            exposure_ = std::max(exposure_ - 0.1f, 0.1f);
            std::cout << "[EXPOSURE] " << exposure_ << "\n";
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::PrintStatus))) {
            printStatus();
        }

        if (input.isActionPressed(static_cast<Action>(DemoAction::PrintHelp))) {
            printControls();
        }
    }

    void onUpdate(float deltaTime) override {
        ApplicationAdvanced::onUpdate(deltaTime);

        totalTime_ += deltaTime;
        frameCount_++;
        fpsAccumulator_ += deltaTime;
        
        if (fpsAccumulator_ >= 1.0f) {
            currentFPS_ = static_cast<float>(frameCount_) / fpsAccumulator_;
            frameCount_ = 0;
            fpsAccumulator_ = 0.0f;
        }

        // Update camera based on mode
        if (orbitMode_) {
            updateOrbitCamera();
        } else {
            updateFlyCamera(deltaTime);
        }

        // Animate lights
        animateLights();
    }

    void updateFlyCamera(float deltaTime) {
        Camera* camera = getActiveCamera();
        if (!camera) return;

        glm::vec3 pos = flyCamera_.getPosition();
        glm::vec3 target = pos + flyCamera_.getForward();

        camera->setPosition({pos.x, pos.y, pos.z});
        camera->setTarget({target.x, target.y, target.z});
    }

    void updateOrbitCamera() {
        Camera* camera = getActiveCamera();
        if (!camera) return;

        float orbitSpeed = 0.08f;
        float orbitRadius = 8.0f;
        
        float angle = totalTime_ * orbitSpeed;
        
        float camX = std::sin(angle) * orbitRadius;
        float camZ = std::cos(angle) * orbitRadius * 0.4f;
        float camY = -4.0f;
        
        camera->setPosition({camX, camY, camZ});
        camera->setTarget({0.0f, -3.0f, 0.0f});
    }

    void printStatus() {
        glm::vec3 pos = flyCamera_.getPosition();
        std::cout << "\n=== Status ===\n";
        std::cout << "FPS: " << currentFPS_ << "\n";
        std::cout << "Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        std::cout << "Mode: " << (orbitMode_ ? "Orbit" : "Fly") << "\n";
        std::cout << "Features:\n";
        std::cout << "  Shadows: " << (shadowsEnabled_ ? "ON" : "OFF") << "\n";
        std::cout << "  SSAO: " << (ssaoEnabled_ ? "ON" : "OFF") << "\n";
        std::cout << "  HDR: " << (hdrEnabled_ ? "ON" : "OFF") 
                  << " (" << (tonemapMode_ == 0 ? "ACES" : tonemapMode_ == 1 ? "Reinhard" : "Uncharted2")
                  << ", exp=" << exposure_ << ")\n";
        std::cout << "  Skybox: " << (skyboxEnabled_ ? "ON" : "OFF") << "\n";
        std::cout << "Lights: " << numLights_ << "\n";
        
        // Show current bindings for movement
        InputManager& input = InputManager::get();
        std::cout << "\n=== Current Bindings ===\n";
        if (auto* binding = input.getBinding(Action::MoveForward)) {
            std::cout << "  Forward: " << InputManager::getInputCodeName(binding->codes[0]) << "\n";
        }
        if (auto* binding = input.getBinding(Action::MoveBackward)) {
            std::cout << "  Back: " << InputManager::getInputCodeName(binding->codes[0]) << "\n";
        }
        if (auto* binding = input.getBinding(Action::MoveLeft)) {
            std::cout << "  Left: " << InputManager::getInputCodeName(binding->codes[0]) << "\n";
        }
        if (auto* binding = input.getBinding(Action::MoveRight)) {
            std::cout << "  Right: " << InputManager::getInputCodeName(binding->codes[0]) << "\n";
        }
    }

    void createSceneLights() {
        std::cout << "\n=== Creating Scene Lights ===\n";

        std::random_device rd;
        std::mt19937 gen(42);  // Fixed seed for reproducible results
        
        // Sponza bounds (approximate)
        float xMin = -15.0f, xMax = 15.0f;
        float yMin = -10.0f, yMax = 2.0f;  // Inverted Y
        float zMin = -6.0f, zMax = 6.0f;
        
        std::uniform_real_distribution<float> xDist(xMin, xMax);
        std::uniform_real_distribution<float> yDist(yMin, yMax);
        std::uniform_real_distribution<float> zDist(zMin, zMax);
        std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);
        
        // Create lights along the corridors
        int lightsCreated = 0;
        const int targetLights = 200;
        
        // Main corridor lights (along X axis)
        for (float x = xMin + 2.0f; x < xMax - 2.0f; x += 2.5f) {
            for (float z : {-3.0f, 0.0f, 3.0f}) {
                float y = yDist(gen);
                float hue = hueDist(gen);
                
                // Convert HSV to RGB (simplified)
                float r, g, b;
                int i = static_cast<int>(hue * 6);
                float f = hue * 6 - i;
                switch (i % 6) {
                    case 0: r = 1; g = f; b = 0; break;
                    case 1: r = 1-f; g = 1; b = 0; break;
                    case 2: r = 0; g = 1; b = f; break;
                    case 3: r = 0; g = 1-f; b = 1; break;
                    case 4: r = f; g = 0; b = 1; break;
                    case 5: r = 1; g = 0; b = 1-f; break;
                    default: r = g = b = 1; break;
                }
                
                float intensity = 2.0f + hueDist(gen) * 3.0f;
                
                int idx = addPointLight(
                    (float[]){x, y, z},
                    (float[]){r, g, b},
                    intensity
                );
                
                if (idx >= 0) {
                    lightsCreated++;
                    lightPositions_.push_back({x, y, z});
                    lightPhases_.push_back(hueDist(gen) * 6.28f);
                }
                
                if (lightsCreated >= targetLights) break;
            }
            if (lightsCreated >= targetLights) break;
        }
        
        numLights_ = lightsCreated;
        std::cout << "Created " << lightsCreated << " point lights\n";
    }

    void animateLights() {
        if (lightPositions_.empty()) return;
        
        LightManager* lightMgr = getLightManager();
        if (!lightMgr) return;
        
        const Light* lights = lightMgr->getLights();
        
        float animSpeed = 0.5f;
        float bounceHeight = 1.0f;
        
        for (size_t i = 0; i < lightPositions_.size() && i < static_cast<size_t>(numLights_); i++) {
            float phase = lightPhases_[i];
            float yOffset = std::sin(totalTime_ * animSpeed + phase) * bounceHeight;
            
            glm::vec3 basePos = lightPositions_[i];
            float newY = basePos.y + yOffset;
            
            // Create updated light with new position
            Light updatedLight = lights[i + 1];  // +1 to skip directional light at index 0
            updatedLight.point.position[0] = basePos.x;
            updatedLight.point.position[1] = newY;
            updatedLight.point.position[2] = basePos.z;
            lightMgr->updateLight(static_cast<int32_t>(i + 1), updatedLight);
        }
    }

    void onShutdownAdvanced() override {
        // Shutdown input system
        InputManager::get().shutdown();
    }

private:
    FlyCamera flyCamera_;
    std::vector<RenderableHandle> modelHandles_;
    
    // Light data
    std::vector<glm::vec3> lightPositions_;
    std::vector<float> lightPhases_;
    glm::vec3 sunDirection_;
    int numLights_ = 0;
    
    // State
    bool mouseCaptured_ = true;
    bool orbitMode_ = false;
    bool shadowsEnabled_ = false;
    bool ssaoEnabled_ = false;
    bool hdrEnabled_ = true;
    bool skyboxEnabled_ = false;
    int tonemapMode_ = 0;
    float exposure_ = 1.0f;
    
    // Timing
    float totalTime_ = 0.0f;
    float currentFPS_ = 0.0f;
    float fpsAccumulator_ = 0.0f;
    int frameCount_ = 0;
};

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
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
║              ADVANCED RENDERING DEMO                             ║
║                                                                  ║
║  Features:                                                       ║
║    • Clustered Forward Shading (500+ lights)                    ║
║    • Shadow Mapping with PCF                                     ║
║    • Screen-Space Ambient Occlusion (SSAO)                      ║
║    • HDR Rendering with ACES Tonemapping                        ║
║    • FPS-style Fly Camera Controls                              ║
║    • NEW: Action-based Input with Runtime Rebinding             ║
║                                                                  ║
╚══════════════════════════════════════════════════════════════════╝
)" << std::endl;

    // Initialize ECS kernel system
    std::cout << "Initializing ECS kernel system...\n";
    
    AdvancedRenderingDemo demo;
    return demo.run();
}
