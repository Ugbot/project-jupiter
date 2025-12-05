/**
 * @file main.cpp
 * @brief Primitives Demo - Showcase procedural geometry shapes
 * 
 * Demonstrates the Jupiter engine's primitive spawning capabilities:
 * - Cubes and boxes
 * - Spheres
 * - Cylinders
 * - Planes/ground
 * - Capsules
 * 
 * Uses the base Application class with auto-rendering.
 */

#include "rendering/application.h"
#include "rendering/lighting.h"
#include "logging/logging.h"
#include "input/input.h"
#include "math/math.h"
#include <cmath>
#include <SDL3/SDL.h>

using namespace jupiter;
using namespace jupiter::rendering;

class PrimitivesDemo : public Application {
public:
    PrimitivesDemo() 
        : Application("Jupiter Primitives Demo", 1280, 720, false) {}  // validation disabled

protected:
    void onInit() override {
        LOG_INFO("Demo", "=== Primitives Demo Starting ===");

        // Initialize input system
        input::InputManager::get().initialize();

        // Enable auto-rendering (uses internal PBR pipeline)
        enableAutoRender();

        // Set up camera - positioned to see the scene well
        camera_ = createPerspectiveCamera(math::PI / 4.0f, 0.0f, 0.1f, 1000.0f);
        camera_->setPosition(math::Vector3(0.0f, 8.0f, 18.0f));
        camera_->setTarget(math::Vector3(0.0f, 1.0f, 0.0f));
        setActiveCamera(camera_);

        // Spawn various primitives in a nice layout (includes ground)
        spawnPrimitiveShowcase();

        // Add lights for good illumination
        setupLighting();

        LOG_INFO("Demo", "=== Demo Initialized ===");
        LOG_INFO("Demo", "Controls:");
        LOG_INFO("Demo", "  WASD - Move camera");
        LOG_INFO("Demo", "  Mouse - Look around");
        LOG_INFO("Demo", "  ESC - Exit");
    }

    void spawnPrimitiveShowcase() {
        float y = 1.0f;  // Height above ground
        float spacing = 3.0f;

        // Row 1: Basic shapes with distinct colors
        {
            // Red cube
            float pos1[3] = {-spacing * 2, y, 0.0f};
            float colorRed[3] = {0.9f, 0.2f, 0.2f};
            cubeHandle_ = spawnCube(pos1, 1.5f, colorRed);
            LOG_INFO("Demo", "Spawned red cube at (%.1f, %.1f, %.1f)", pos1[0], pos1[1], pos1[2]);

            // Blue sphere
            float pos2[3] = {-spacing, y, 0.0f};
            float colorBlue[3] = {0.2f, 0.4f, 0.9f};
            sphereHandle_ = spawnSphere(pos2, 0.8f, 32, 16, colorBlue);
            LOG_INFO("Demo", "Spawned blue sphere at (%.1f, %.1f, %.1f)", pos2[0], pos2[1], pos2[2]);

            // Green cylinder
            float pos3[3] = {0.0f, y, 0.0f};
            float colorGreen[3] = {0.2f, 0.8f, 0.3f};
            cylinderHandle_ = spawnCylinder(pos3, 0.5f, 2.0f, 32, colorGreen);
            LOG_INFO("Demo", "Spawned green cylinder at (%.1f, %.1f, %.1f)", pos3[0], pos3[1], pos3[2]);

            // Orange box (non-uniform)
            float pos4[3] = {spacing, y, 0.0f};
            float dims[3] = {2.0f, 0.5f, 1.0f};
            float colorOrange[3] = {1.0f, 0.5f, 0.1f};
            boxHandle_ = spawnBox(pos4, dims, colorOrange);
            LOG_INFO("Demo", "Spawned orange box at (%.1f, %.1f, %.1f)", pos4[0], pos4[1], pos4[2]);

            // Purple capsule
            float pos5[3] = {spacing * 2, y + 0.5f, 0.0f};
            float colorPurple[3] = {0.7f, 0.2f, 0.9f};
            capsuleHandle_ = spawnCapsule(pos5, 0.4f, 2.0f, colorPurple);
            LOG_INFO("Demo", "Spawned purple capsule at (%.1f, %.1f, %.1f)", pos5[0], pos5[1], pos5[2]);
        }

        // Row 2: Smaller colorful shapes in background
        float zBack = -spacing;
        {
            // Rainbow of small cubes
            float colors[5][3] = {
                {1.0f, 0.3f, 0.3f},  // Red
                {1.0f, 0.8f, 0.2f},  // Yellow
                {0.3f, 1.0f, 0.3f},  // Green
                {0.3f, 0.6f, 1.0f},  // Blue
                {0.9f, 0.3f, 0.9f}   // Magenta
            };
            for (int i = 0; i < 5; ++i) {
                float x = (i - 2) * spacing;
                float pos[3] = {x, 0.5f, zBack};
                spawnCube(pos, 0.5f, colors[i]);
            }
        }

        // Row 3: Larger metallic-looking shapes in front
        float zFront = spacing;
        {
            // Gold sphere
            float pos1[3] = {-spacing, 1.5f, zFront};
            float colorGold[3] = {0.95f, 0.8f, 0.3f};
            spawnSphere(pos1, 1.2f, 48, 24, colorGold);

            // Silver cube
            float pos2[3] = {spacing, 1.0f, zFront};
            float colorSilver[3] = {0.8f, 0.8f, 0.85f};
            spawnCube(pos2, 2.0f, colorSilver);
        }

        // Ground plane (gray)
        float groundColor[3] = {0.4f, 0.4f, 0.45f};
        float groundPos[3] = {0.0f, -0.5f, 0.0f};
        groundHandle_ = spawnPlane(groundPos, 25.0f, 25.0f, groundColor);
    }

    void setupLighting() {
        // Main directional light (sun) - warm sunlight from upper-right
        float sunDir[3] = {0.6f, -0.7f, 0.4f};  // Direction toward light
        float sunColor[3] = {1.0f, 0.95f, 0.85f};  // Warm white
        addDirectionalLight(sunDir, sunColor, 2.5f);  // Reasonable intensity
        LOG_INFO("Demo", "Added main sun light");

        // Secondary directional light (sky fill) - cooler from opposite side
        float fillDir[3] = {-0.3f, -0.6f, -0.5f};
        float fillColor[3] = {0.5f, 0.6f, 0.8f};  // Cool sky blue
        addDirectionalLight(fillDir, fillColor, 0.8f);
        LOG_INFO("Demo", "Added fill light");

        // Warm accent point light (front-left)
        float pointColor1[3] = {1.0f, 0.6f, 0.3f};
        float pointPos1[3] = {-6.0f, 4.0f, 6.0f};
        addPointLight(pointPos1, pointColor1, 15.0f, 20.0f);

        // Cool accent point light (back-right)
        float pointColor2[3] = {0.3f, 0.5f, 1.0f};
        float pointPos2[3] = {6.0f, 4.0f, -6.0f};
        addPointLight(pointPos2, pointColor2, 12.0f, 18.0f);

        // Rim light (behind)
        float rimColor[3] = {0.9f, 0.9f, 1.0f};
        float rimPos[3] = {0.0f, 6.0f, -10.0f};
        addPointLight(rimPos, rimColor, 10.0f, 25.0f);

        // Ambient fill - subtle
        float ambientColor[3] = {0.3f, 0.35f, 0.4f};
        setAmbientLight(ambientColor, 0.3f);

        // Orbiting spot light - colorful
        float spotPos[3] = {8.0f, 5.0f, 0.0f};
        float spotDir[3] = {-1.0f, -0.4f, 0.0f};
        float spotColor[3] = {0.2f, 1.0f, 0.8f};  // Cyan
        spotLightIndex_ = addSpotLight(spotPos, spotDir, spotColor, 25.0f, 0.4f, 0.6f);
        LOG_INFO("Demo", "Added orbiting spot light (index: %d)", spotLightIndex_);
        
        LOG_INFO("Demo", "Lighting setup complete");
    }

    void onUpdate(float deltaTime) override {
        totalTime_ += deltaTime;

        // Animate the primitives to show dynamic lighting
        animatePrimitives();
        
        // Animate the orbiting spot light
        animateSpotLight();

        // Update camera based on input
        processInput(deltaTime);
    }
    
    void animateSpotLight() {
        if (spotLightIndex_ < 0) return;
        
        LightManager* lightMgr = getLightManager();
        if (!lightMgr) return;
        
        // Orbit parameters - faster and closer for more dramatic effect
        float orbitRadius = 12.0f;
        float orbitHeight = 6.0f;
        float orbitSpeed = 0.8f;
        
        // Calculate position on orbit
        float angle = totalTime_ * orbitSpeed;
        float x = orbitRadius * std::cos(angle);
        float z = orbitRadius * std::sin(angle);
        float y = orbitHeight + 1.0f * std::sin(totalTime_ * 0.7f);  // Slight vertical bob
        
        // Calculate direction pointing at center (0, 1, 0)
        float targetY = 1.0f;  // Look at center of scene
        float dx = 0.0f - x;
        float dy = targetY - y;
        float dz = 0.0f - z;
        
        // Normalize direction
        float len = std::sqrt(dx*dx + dy*dy + dz*dz);
        dx /= len;
        dy /= len;
        dz /= len;
        
        // Color cycling - smooth rainbow transition
        float hue = std::fmod(totalTime_ * 0.2f, 1.0f);  // Slow color cycle
        float r, g, b;
        // HSV to RGB (simplified for saturation=1, value=1)
        int i = static_cast<int>(hue * 6.0f);
        float f = hue * 6.0f - i;
        switch (i % 6) {
            case 0: r = 1.0f; g = f;    b = 0.0f; break;
            case 1: r = 1-f;  g = 1.0f; b = 0.0f; break;
            case 2: r = 0.0f; g = 1.0f; b = f;    break;
            case 3: r = 0.0f; g = 1-f;  b = 1.0f; break;
            case 4: r = f;    g = 0.0f; b = 1.0f; break;
            default: r = 1.0f; g = 0.0f; b = 1-f;  break;
        }

        // Update the spot light
        Light updatedLight;
        updatedLight.type = static_cast<uint32_t>(LightType::SPOT);
        updatedLight.spot.position[0] = x;
        updatedLight.spot.position[1] = y;
        updatedLight.spot.position[2] = z;
        updatedLight.spot.direction[0] = dx;
        updatedLight.spot.direction[1] = dy;
        updatedLight.spot.direction[2] = dz;
        updatedLight.spot.color[0] = r;
        updatedLight.spot.color[1] = g;
        updatedLight.spot.color[2] = b;
        updatedLight.spot.intensity = 25.0f;  // Match setup intensity
        updatedLight.spot.innerConeAngle = 0.4f;
        updatedLight.spot.outerConeAngle = 0.6f;
        
        lightMgr->updateLight(spotLightIndex_, updatedLight);
    }

    void animatePrimitives() {
        float t = totalTime_;
        
        // Cube: Spin in place
        if (cubeHandle_.isValid()) {
            float pos[3] = {-6.0f, 1.0f + 0.5f * std::sin(t * 2.0f), 0.0f};
            float rot[3] = {t * 0.5f, t * 0.8f, t * 0.3f};  // pitch, yaw, roll
            setRenderableTransform(cubeHandle_, pos, rot, 1.0f);
        }
        
        // Sphere: Orbit around center
        if (sphereHandle_.isValid()) {
            float radius = 3.0f;
            float speed = 0.7f;
            float pos[3] = {
                radius * std::cos(t * speed),
                1.5f + 0.5f * std::sin(t * 1.5f),
                radius * std::sin(t * speed)
            };
            setRenderableTransform(sphereHandle_, pos);
        }
        
        // Cylinder: Bounce up and down while rotating
        if (cylinderHandle_.isValid()) {
            float pos[3] = {0.0f, 1.0f + std::abs(std::sin(t * 2.0f)), 0.0f};
            float rot[3] = {0.0f, t * 1.2f, 0.0f};
            setRenderableTransform(cylinderHandle_, pos, rot);
        }
        
        // Box: Figure-8 pattern
        if (boxHandle_.isValid()) {
            float pos[3] = {
                3.0f * std::sin(t * 0.8f),
                1.0f + 0.3f * std::sin(t * 3.0f),
                1.5f * std::sin(t * 1.6f)
            };
            float rot[3] = {t * 0.4f, t * 0.6f, 0.0f};
            setRenderableTransform(boxHandle_, pos, rot);
        }
        
        // Capsule: Pendulum swing
        if (capsuleHandle_.isValid()) {
            float swingAngle = 0.5f * std::sin(t * 1.5f);
            float pos[3] = {6.0f + 2.0f * std::sin(swingAngle), 2.0f, 0.0f};
            float rot[3] = {0.0f, 0.0f, swingAngle};
            setRenderableTransform(capsuleHandle_, pos, rot);
        }
    }

    void onInput(float deltaTime) override {
        input::InputManager::get().update();
        
        // Check for exit
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                requestClose();
                return;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    requestClose();
                    return;
                }
            }
        }
    }

    void processInput(float deltaTime) {
        if (!camera_) return;
        
        const bool* keyState = SDL_GetKeyboardState(nullptr);
        float moveSpeed = 5.0f * deltaTime;

        // Get current position
        math::Vector3 pos = camera_->getPosition();
        math::Vector3 target = camera_->getTarget();

        // Simple WASD movement in world space
        if (keyState[SDL_SCANCODE_W]) {
            pos.z -= moveSpeed;
            target.z -= moveSpeed;
        }
        if (keyState[SDL_SCANCODE_S]) {
            pos.z += moveSpeed;
            target.z += moveSpeed;
        }
        if (keyState[SDL_SCANCODE_A]) {
            pos.x -= moveSpeed;
            target.x -= moveSpeed;
        }
        if (keyState[SDL_SCANCODE_D]) {
            pos.x += moveSpeed;
            target.x += moveSpeed;
        }
        if (keyState[SDL_SCANCODE_Q]) {
            pos.y -= moveSpeed;
            target.y -= moveSpeed;
        }
        if (keyState[SDL_SCANCODE_E]) {
            pos.y += moveSpeed;
            target.y += moveSpeed;
        }

        camera_->setPosition(pos);
        camera_->setTarget(target);
    }

    void onShutdown() override {
        LOG_INFO("Demo", "Shutting down Primitives Demo");
        input::InputManager::get().shutdown();
    }

private:
    float totalTime_ = 0.0f;
    PerspectiveCamera* camera_ = nullptr;
    int32_t spotLightIndex_ = -1;

    // Handles to main primitives (for animation)
    RenderableHandle groundHandle_;
    RenderableHandle cubeHandle_;
    RenderableHandle sphereHandle_;
    RenderableHandle cylinderHandle_;
    RenderableHandle boxHandle_;
    RenderableHandle capsuleHandle_;
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    logging::initialize();
    LOG_INFO("Main", "Starting Primitives Demo");

    PrimitivesDemo demo;
    int result = demo.run();

    LOG_INFO("Main", "Demo exited with code %d", result);
    return result;
}

