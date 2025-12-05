#include "rendering/application.h"
#include "logging/logging.h"
#include "math/math.h"
#include <cmath>

using namespace jupiter::rendering;
using namespace jupiter::math;

/**
 * @brief PBR Lighting Demo with GLTF Model (New simplified API)
 *
 * Demonstrates:
 * - Simple GLTF model loading with automatic rendering
 * - PBR materials and textures
 * - Dynamic lighting
 * - Automatic camera and scene management
 */
class PBRDemoApplication : public Application {
public:
    PBRDemoApplication()
        : Application("PBR Lighting Demo - DamagedHelmet", 1280, 720, false)  // Validation layers not available
        , cameraAngle_(0.0f) {
    }

protected:
    void onInit() override {
        LOG_INFO("PBRDemo", "======================================== ==");
        LOG_INFO("PBRDemo", "  Initializing PBR Lighting Demo");
        LOG_INFO("PBRDemo", "========================================");

        // Enable automatic PBR rendering (makes onRender() optional)
        enableAutoRender();

        // Create perspective camera
        auto* camera = createPerspectiveCamera(
            PI / 4.0f,                              // 45-degree FOV
            (float)getWidth() / getHeight(),        // Aspect ratio
            0.1f,                                   // Near plane
            100.0f                                  // Far plane
        );

        // Position camera looking at origin
        Vector3 cameraPos(0.0f, 2.0f, 5.0f);
        camera->lookAt(cameraPos, Vector3::zero(), Vector3::up());
        setActiveCamera(camera);

        LOG_INFO("PBRDemo", "✓ Created perspective camera");

        // Load GLTF model (automatically creates GPU resources and adds to scene)
        LOG_INFO("PBRDemo", "Loading DamagedHelmet model...");
        auto handles = loadModel("models/DamagedHelmet/DamagedHelmet.gltf");

        if (handles.empty()) {
            LOG_ERROR("PBRDemo", "Failed to load model!");
            LOG_ERROR("PBRDemo", "Make sure models/DamagedHelmet/DamagedHelmet.gltf exists");
            return;
        }

        LOG_INFO("PBRDemo", "✓ Loaded model with {} renderable meshes", handles.size());

        // Add directional light (sun)
        float sunDir[3] = {-0.5f, -1.0f, -0.5f};
        float white[3] = {1.0f, 1.0f, 1.0f};
        addDirectionalLight(sunDir, white, 8.0f);  // Increased from 2.0
        LOG_INFO("PBRDemo", "✓ Added directional light (sun)");

        // Add point lights for accent lighting
        float pointPos1[3] = {2.0f, 2.0f, 2.0f};
        float lightColor1[3] = {1.0f, 0.9f, 0.8f};  // Warm white
        addPointLight(pointPos1, lightColor1, 15.0f, 10.0f);  // Increased from 3.0

        float pointPos2[3] = {-2.0f, 1.0f, 1.0f};
        float lightColor2[3] = {0.3f, 0.5f, 1.0f};  // Blue accent
        addPointLight(pointPos2, lightColor2, 8.0f, 10.0f);  // Increased from 1.5
        LOG_INFO("PBRDemo", "✓ Added {} point lights", 2);

        // Set ambient light
        float ambientColor[3] = {0.3f, 0.3f, 0.35f};  // Increased ambient
        setAmbientLight(ambientColor, 1.5f);  // Increased from 0.3
        LOG_INFO("PBRDemo", "✓ Set ambient light");

        LOG_INFO("PBRDemo", "");
        LOG_INFO("PBRDemo", "========================================");
        LOG_INFO("PBRDemo", "  PBR Demo Ready!");
        LOG_INFO("PBRDemo", "========================================");
        LOG_INFO("PBRDemo", "");
        LOG_INFO("PBRDemo", "Features:");
        LOG_INFO("PBRDemo", "  ✓ Automatic PBR rendering");
        LOG_INFO("PBRDemo", "  ✓ GLTF model with materials");
        LOG_INFO("PBRDemo", "  ✓ Albedo + Metallic/Roughness textures");
        LOG_INFO("PBRDemo", "  ✓ Dynamic lighting (1 directional + 2 point lights)");
        LOG_INFO("PBRDemo", "  ✓ Automatic camera orbit");
        LOG_INFO("PBRDemo", "");
    }

    void onUpdate(float deltaTime) override {
        // Rotate camera around the model (orbit animation)
        cameraAngle_ += deltaTime * 0.3f;  // ~18 degrees per second

        float radius = 5.0f;
        float camX = std::sin(cameraAngle_) * radius;
        float camZ = std::cos(cameraAngle_) * radius;
        float camY = 2.0f;

        auto* camera = getActiveCamera();
        if (camera) {
            Vector3 cameraPos(camX, camY, camZ);
            camera->lookAt(cameraPos, Vector3::zero(), Vector3::up());
        }
    }

    // onRender() is optional when using enableAutoRender()
    // The scene is automatically rendered by the Application framework

private:
    float cameraAngle_;
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    LOG_INFO("Main", "");
    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "   PBR Lighting Demo");
    LOG_INFO("Main", "   DamagedHelmet with PBR Materials");
    LOG_INFO("Main", "   Project Jupiter Engine");
    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "");

    PBRDemoApplication app;
    return app.run();
}
