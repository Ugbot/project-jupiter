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
#include "rendering/pbr_push_constants.h"
#include "input/input.h"
#include "ecs/ecs.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL.h>
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
        if (input.isActionHeld(Action::MoveUp)) position_.y += velocity;    // Up
        if (input.isActionHeld(Action::MoveDown)) position_.y -= velocity;  // Down
    }

    void processMouse(float xoffset, float yoffset) {
        yaw_ -= xoffset * mouseSensitivity_;    // Invert X
        pitch_ -= yoffset * mouseSensitivity_;  // Invert Y

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
        
        // Set up camera for Sponza (matches HelloVulkan reference: near=0.1, far=100)
        auto* camera = createPerspectiveCamera(
            glm::radians(45.0f),   // Standard FOV (matches reference)
            0.0f,                   // Auto aspect
            0.1f,                   // Near
            100.0f                  // Far (matches reference)
        );
        setActiveCamera(camera);

        // Initialize fly camera - position like HelloVulkan: (0, 1, 6) looking at (0, 2.5, 0)
        flyCamera_.setPosition({0.0f, 1.0f, 6.0f});
        flyCamera_.setYawPitch(glm::radians(-90.0f), glm::radians(15.0f));  // Looking towards center
        flyCamera_.setMoveSpeed(2.5f);  // Slower movement for smaller scale

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
            
            // Scale model down to fit camera frustum (Sponza is ~30m, we want ~3m)
            glm::mat4 scaleTransform = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
            
            SceneManager* scene = getSceneManager();
            for (auto& handle : modelHandles_) {
                scene->updateTransform(handle, scaleTransform);
            }
        } else {
            std::cerr << "ERROR: Could not load any model!\n";
        }

        // Set up main directional light (matches lighting_demo)
        sunDirection_ = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));
        addDirectionalLight(
            (float[]){sunDirection_.x, sunDirection_.y, sunDirection_.z},
            (float[]){1.0f, 1.0f, 1.0f},
            8.0f  // Same as lighting_demo
        );

        // Ambient light (matches lighting_demo)
        setAmbientLight((float[]){0.3f, 0.3f, 0.35f}, 1.5f);

        // Add point lights like lighting_demo (scaled for 0.01 model scale)
        float pointPos1[3] = {0.02f, 0.02f, 0.02f};  // Scaled from {2, 2, 2}
        float lightColor1[3] = {1.0f, 0.9f, 0.8f};
        addPointLight(pointPos1, lightColor1, 15.0f, 0.1f);  // Scaled radius

        float pointPos2[3] = {-0.02f, 0.01f, 0.01f};  // Scaled from {-2, 1, 1}
        float lightColor2[3] = {0.3f, 0.5f, 1.0f};
        addPointLight(pointPos2, lightColor2, 8.0f, 0.1f);

        // IBL Setup - using fallback gray environment
        // Full HDR IBL pipeline needs further debugging (causes VK_ERROR_DEVICE_LOST on Apple M3)
        // Compared with HelloVulkan: render pass structure matches, but MRT rendering still fails
        // TODO: Try RenderDoc capture to identify exact failure point
        std::cout << "\n=== IBL Setup ===\n";
        std::cout << "Using computed BRDF LUT with fallback environment\n";
        
        // Set PBR parameters
        setAmbientIntensity(1.0f);        // IBL ambient contribution
        setExposure(1.0f);                // Manual exposure
        setMaxReflectionLod(4.0f);        // IBL mipmap levels
        setLightFalloff(1.0f);            // Light attenuation power
        setAlbedoMultiplier(0.01f);       // Slight ambient to prevent pure black
        
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
        
        // Disable all advanced features by default until fully debugged
        // features.enable(RenderFeature::ShadowMapping);
        // features.enable(RenderFeature::SSAO);
        // features.enable(RenderFeature::Tonemap);
        
        shadowsEnabled_ = false;
        ssaoEnabled_ = false;
        hdrEnabled_ = false;
        skyboxEnabled_ = false;
        
        std::cout << "  [X] Shadow Mapping (2048x2048, PCF)\n";
        std::cout << "  [X] SSAO (64 samples)\n";
        std::cout << "  [X] HDR Tonemapping (ACES)\n";
        std::cout << "  [ ] Skybox\n";
        
        // Update shadow light for the sun direction
        glm::vec3 lightPos = sunDirection_ * 50.0f;  // Position far away in sun direction
        glm::vec3 target(0.0f, 0.0f, 0.0f);          // Scene center
        updateShadowLight(lightPos, -sunDirection_, target);
        
        // Set up PBR parameters from HelloVulkan for best results
        // These can be tuned in real-time with the debug controls
        setAmbientIntensity(1.0f);        // IBL ambient contribution
        setExposure(1.0f);                // Manual exposure
        setMaxReflectionLod(4.0f);        // IBL mipmap levels
        setLightFalloff(1.0f);            // Light attenuation power
        setAlbedoMultiplier(0.01f);       // Slight ambient to prevent pure black (from HelloVulkan)
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
        
        // Handle PBR debug keys via direct SDL polling
        handleDebugKeys();
        
        // Handle continuous movement
        if (mouseCaptured_ && !orbitMode_) {
            flyCamera_.processInput(input, deltaTime);
            
            // Process mouse look
            float mouseDx, mouseDy;
            input.getMouseDelta(mouseDx, mouseDy);
            flyCamera_.processMouse(mouseDx, mouseDy);
        }
    }
    
    void handleDebugKeys() {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        static bool lastKeys[SDL_SCANCODE_COUNT] = {false};
        
        // Track toggle states separately from debug view modes
        static uint32_t toggleFlags = 0;  // T, I, N, L toggles
        static uint32_t debugViewFlag = 0; // F1-F10 debug view mode
        
        auto checkKey = [&](SDL_Scancode sc) -> bool {
            bool pressed = keys[sc] && !lastKeys[sc];
            lastKeys[sc] = keys[sc];
            return pressed;
        };
        
        bool changed = false;
        
        // Debug view modes (F1-F10) - these are mutually exclusive
        if (checkKey(SDL_SCANCODE_KP_0) || checkKey(SDL_SCANCODE_GRAVE)) {
            debugViewFlag = 0;
            std::cout << "[DEBUG] Normal rendering\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F1)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_ALBEDO;
            std::cout << "[DEBUG] Showing ALBEDO texture\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F2)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_NORMALS;
            std::cout << "[DEBUG] Showing NORMALS\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F3)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_METALLIC;
            std::cout << "[DEBUG] Showing METALLIC\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F4)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_ROUGHNESS;
            std::cout << "[DEBUG] Showing ROUGHNESS\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F5)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_AO;
            std::cout << "[DEBUG] Showing AO\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F6)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_DIFFUSE_ONLY;
            std::cout << "[DEBUG] Showing DIFFUSE only\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F7)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_SPECULAR_ONLY;
            std::cout << "[DEBUG] Showing SPECULAR only\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F8)) {
            debugViewFlag = PBRPushConstants::FLAG_DEBUG_F0;
            std::cout << "[DEBUG] Showing F0\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F9)) {
            debugViewFlag = (1u << 16);  // Albedo * baseColorFactor
            std::cout << "[DEBUG] Showing ALBEDO * baseColorFactor\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_F10)) {
            debugViewFlag = (1u << 17);  // baseColorFactor UBO value
            std::cout << "[DEBUG] Showing baseColorFactor UBO\n";
            changed = true;
        }
        
        // Toggle flags (T, I, N, L) - these persist across debug view changes
        if (checkKey(SDL_SCANCODE_T)) {
            toggleFlags ^= PBRPushConstants::FLAG_DISABLE_TONEMAPPING;
            std::cout << "[DEBUG] Tonemapping: " << ((toggleFlags & PBRPushConstants::FLAG_DISABLE_TONEMAPPING) ? "OFF" : "ON") << "\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_I)) {
            toggleFlags ^= PBRPushConstants::FLAG_DISABLE_IBL;
            std::cout << "[DEBUG] IBL: " << ((toggleFlags & PBRPushConstants::FLAG_DISABLE_IBL) ? "OFF" : "ON") << "\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_N)) {
            toggleFlags ^= PBRPushConstants::FLAG_DISABLE_NORMAL_MAPPING;
            std::cout << "[DEBUG] Normal mapping: " << ((toggleFlags & PBRPushConstants::FLAG_DISABLE_NORMAL_MAPPING) ? "OFF" : "ON") << "\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_L)) {
            toggleFlags ^= PBRPushConstants::FLAG_DISABLE_DIRECT_LIGHT;
            std::cout << "[DEBUG] Direct light: " << ((toggleFlags & PBRPushConstants::FLAG_DISABLE_DIRECT_LIGHT) ? "OFF" : "ON") << "\n";
            changed = true;
        }
        if (checkKey(SDL_SCANCODE_P)) {
            printDebugHelp();
        }
        
        // Combine toggle flags with debug view flag
        if (changed) {
            uint32_t finalFlags = toggleFlags | debugViewFlag;
            setPBRDebugFlags(finalFlags);
        }
    }
    
    void printDebugHelp() {
        std::cout << "\n========================================\n";
        std::cout << "  PBR Debug Controls:\n";
        std::cout << "========================================\n";
        std::cout << "  ` or KP0 = Normal rendering\n";
        std::cout << "  F1 = Albedo texture\n";
        std::cout << "  F2 = Normals\n";
        std::cout << "  F3 = Metallic\n";
        std::cout << "  F4 = Roughness\n";
        std::cout << "  F5 = Ambient Occlusion\n";
        std::cout << "  F6 = Diffuse contribution only\n";
        std::cout << "  F7 = Specular contribution only\n";
        std::cout << "  F8 = F0 (reflectivity)\n";
        std::cout << "  F9 = Albedo * baseColorFactor\n";
        std::cout << "  F10 = baseColorFactor UBO value\n";
        std::cout << "  T = Toggle tonemapping\n";
        std::cout << "  I = Toggle IBL\n";
        std::cout << "  N = Toggle normal mapping\n";
        std::cout << "  L = Toggle direct lighting\n";
        std::cout << "  P = Print this help\n";
        std::cout << "========================================\n\n";
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
            flyCamera_.setPosition({0.0f, 1.0f, 6.0f});
            flyCamera_.setYawPitch(glm::radians(-90.0f), glm::radians(15.0f));
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
