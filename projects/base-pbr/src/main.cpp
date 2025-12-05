/**
 * Base PBR - Minimal PBR rendering reference
 *
 * A simple demonstration of PBR (Physically Based Rendering) using the engine's
 * rendering framework. Based on vendored/vulkan-gltf-pbr reference implementation.
 */

#include "rendering/application.h"
#include "logging/logging.h"
#include "math/math.h"
#include <cmath>

using namespace jupiter::rendering;
using namespace jupiter::math;

class BasePBRApp : public Application {
public:
    BasePBRApp()
        : Application("Base PBR - Minimal Reference", 1280, 720, false)
        , cameraAngle_(0.0f) {
    }

protected:
    void onInit() override {
        LOG_INFO("BasePBR", "========================================");
        LOG_INFO("BasePBR", "  Base PBR - Minimal Reference");
        LOG_INFO("BasePBR", "========================================");

        // Enable automatic PBR rendering
        enableAutoRender();

        // Create perspective camera
        auto* camera = createPerspectiveCamera(
            PI / 4.0f,                           // 45-degree FOV
            (float)getWidth() / getHeight(),     // Aspect ratio
            0.1f,                                 // Near plane
            100.0f                               // Far plane
        );

        // Position camera
        Vector3 cameraPos(0.0f, 0.5f, 3.0f);
        camera->lookAt(cameraPos, Vector3::zero(), Vector3::up());
        setActiveCamera(camera);

        LOG_INFO("BasePBR", "Created perspective camera");

        // Load DamagedHelmet model
        LOG_INFO("BasePBR", "Loading DamagedHelmet model...");
        auto handles = loadModel("bin/models/DamagedHelmet/DamagedHelmet.gltf");

        if (handles.empty()) {
            LOG_ERROR("BasePBR", "Failed to load model!");
            return;
        }

        LOG_INFO("BasePBR", "Loaded model with {} renderable meshes", handles.size());

        // Add single directional light (simple like reference)
        float lightDir[3] = {0.0f, -1.0f, -0.5f};
        float white[3] = {1.0f, 1.0f, 1.0f};
        addDirectionalLight(lightDir, white, 1.0f);

        LOG_INFO("BasePBR", "Added directional light");
        LOG_INFO("BasePBR", "");
        LOG_INFO("BasePBR", "========================================");
        LOG_INFO("BasePBR", "  Base PBR Ready!");
        LOG_INFO("BasePBR", "========================================");
    }

    void onUpdate(float deltaTime) override {
        // Simple camera orbit
        cameraAngle_ += deltaTime * 0.5f;

        float radius = 3.0f;
        Vector3 cameraPos(
            std::sin(cameraAngle_) * radius,
            0.5f,
            std::cos(cameraAngle_) * radius
        );

        auto* camera = getActiveCamera();
        if (camera) {
            camera->lookAt(cameraPos, Vector3::zero(), Vector3::up());
        }
    }

private:
    float cameraAngle_;
};

int main() {
    BasePBRApp app;
    return app.run();
}
