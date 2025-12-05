/**
 * Animation Demo - Skeletal Animation with GLTF
 *
 * Demonstrates loading and displaying animated GLTF models.
 * Based on the oryol-samples Dragons demo concept.
 *
 * Phase 1: Load and display animated model (using existing PBR pipeline)
 * Phase 2: Add animation evaluation (TODO)
 * Phase 3: Add GPU instancing for many characters (TODO)
 */

#include "rendering/application.h"
#include "math/math.h"
#include "logging/logging.h"
#include <cmath>

using namespace jupiter;
using namespace jupiter::rendering;

class AnimationDemo : public Application {
public:
    AnimationDemo() : Application("Animation Demo - DancingStormtrooper", 1280, 720) {}

protected:
    void onInit() override {
        LOG_INFO("AnimationDemo", "===========================================");
        LOG_INFO("AnimationDemo", "Animation Demo - Skeletal Animation");
        LOG_INFO("AnimationDemo", "===========================================");

        // Enable PBR auto-rendering
        enableAutoRender();

        // Create camera
        auto* camera = createPerspectiveCamera(
            math::PI / 4.0f,  // 45 degree FOV
            0.0f,             // Auto aspect ratio
            0.1f,             // Near plane
            100.0f            // Far plane
        );

        // Position camera to view the character (same as lighting_demo)
        camera->lookAt(math::Vector3(0.0f, 2.0f, 5.0f), math::Vector3::zero(), math::Vector3::up());
        setActiveCamera(camera);

        // Load the animated DancingStormtrooper model
        LOG_INFO("AnimationDemo", "Loading animated model...");
        auto handles = loadModel("models/DancingStormtrooper01/DancingStormtrooper01.gltf");

        if (handles.empty()) {
            LOG_ERROR("AnimationDemo", "Failed to load DancingStormtrooper model!");
            return;
        }

        LOG_INFO("AnimationDemo", "Loaded %zu renderables from model", handles.size());

        // Setup lighting
        setupLighting();

        LOG_INFO("AnimationDemo", "Animation demo initialized!");
        LOG_INFO("AnimationDemo", "Camera orbits around the character");
    }

    void onUpdate(float deltaTime) override {
        // Update camera to orbit around the character (same as lighting_demo)
        cameraAngle_ += deltaTime * 0.3f;
        float radius = 5.0f;
        float height = 2.0f;

        float camX = std::sin(cameraAngle_) * radius;
        float camZ = std::cos(cameraAngle_) * radius;

        auto* camera = getActiveCamera();
        if (camera) {
            math::Vector3 cameraPos(camX, height, camZ);
            camera->lookAt(cameraPos, math::Vector3::zero(), math::Vector3::up());
        }

        // Update time for animation display
        animTime_ += deltaTime;
    }

    void onShutdown() override {
        LOG_INFO("AnimationDemo", "Shutting down animation demo...");
    }

private:
    void setupLighting() {
        // Main directional light (sun)
        float sunDir[] = { 0.5f, 1.0f, 0.3f };
        float sunColor[] = { 1.0f, 0.98f, 0.95f };
        addDirectionalLight(sunDir, sunColor, 2.0f);

        // Fill light from opposite side
        float fillDir[] = { -0.5f, 0.5f, -0.5f };
        float fillColor[] = { 0.6f, 0.7f, 0.9f };
        addDirectionalLight(fillDir, fillColor, 0.5f);

        // Ambient light
        float ambientColor[] = { 0.2f, 0.25f, 0.3f };
        setAmbientLight(ambientColor, 0.3f);
    }

    float cameraAngle_ = 0.0f;
    float animTime_ = 0.0f;
};

int main(int argc, char* argv[]) {
    AnimationDemo demo;
    return demo.run();
}
