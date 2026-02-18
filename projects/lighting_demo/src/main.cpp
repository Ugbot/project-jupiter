#include "rendering/application.h"
#include "rendering/pbr_push_constants.h"
#include "logging/logging.h"
#include "math/math.h"
#include <cmath>
#include <SDL3/SDL.h>

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

        // Optional heavy IBL generation (can stall on some GPUs)
        // Default: disabled to avoid driver hangs; set env JUPITER_LIGHTING_DEMO_IBL=1 to enable
        const char* iblEnv = std::getenv("JUPITER_LIGHTING_DEMO_IBL");
        if (iblEnv && std::strcmp(iblEnv, "1") == 0) {
            if (loadIBLFromHDR("Assets/Textures/piazza_bologni_1k.hdr")) {
                LOG_INFO("PBRDemo", "✓ Loaded HDR environment map for IBL");
            } else {
                LOG_WARN("PBRDemo", "Using fallback IBL (HDR file not found or load failed)");
                setIBLEnabled(false);
            }
        } else {
            LOG_WARN("PBRDemo", "IBL generation disabled by default. Set JUPITER_LIGHTING_DEMO_IBL=1 to enable.");
            setIBLEnabled(false);
            auto& settings = getRenderSettingsMutable();
            settings.maxReflectionLodClamp = 0.0f; // Avoid sampling non-existent mips on fallback
            applyRenderSettings(settings);
        }

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
        addDirectionalLight(sunDir, white, 2.5f);  // Balanced sun intensity
        LOG_INFO("PBRDemo", "✓ Added directional light (sun)");

        // Add point lights for accent lighting (balanced intensities)
        float pointPos1[3] = {2.0f, 2.0f, 2.0f};
        float lightColor1[3] = {1.0f, 0.9f, 0.8f};  // Warm white
        addPointLight(pointPos1, lightColor1, 2.0f, 10.0f);

        float pointPos2[3] = {-2.0f, 1.0f, 1.0f};
        float lightColor2[3] = {0.3f, 0.5f, 1.0f};  // Blue accent
        addPointLight(pointPos2, lightColor2, 1.0f, 10.0f);
        LOG_INFO("PBRDemo", "✓ Added {} point lights", 2);

        // Set ambient light (low to preserve color saturation)
        float ambientColor[3] = {0.2f, 0.2f, 0.25f};
        setAmbientLight(ambientColor, 0.3f);
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

    void onInput(float /*deltaTime*/) override {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                requestClose();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                handleKeyPress(event.key.scancode);
            }
        }
    }

    void handleKeyPress(SDL_Scancode scancode) {
        uint32_t flags = getPBRDebugFlags();
        bool changed = true;
        
        switch (scancode) {
            case SDL_SCANCODE_0: // Normal rendering
                flags = 0;
                LOG_INFO("Debug", "Normal rendering");
                break;
            case SDL_SCANCODE_1: // Albedo
                flags = PBRPushConstants::FLAG_DEBUG_ALBEDO;
                LOG_INFO("Debug", "Showing ALBEDO texture");
                break;
            case SDL_SCANCODE_2: // Normals
                flags = PBRPushConstants::FLAG_DEBUG_NORMALS;
                LOG_INFO("Debug", "Showing NORMALS");
                break;
            case SDL_SCANCODE_3: // Metallic
                flags = PBRPushConstants::FLAG_DEBUG_METALLIC;
                LOG_INFO("Debug", "Showing METALLIC");
                break;
            case SDL_SCANCODE_4: // Roughness
                flags = PBRPushConstants::FLAG_DEBUG_ROUGHNESS;
                LOG_INFO("Debug", "Showing ROUGHNESS");
                break;
            case SDL_SCANCODE_5: // AO
                flags = PBRPushConstants::FLAG_DEBUG_AO;
                LOG_INFO("Debug", "Showing AO");
                break;
            case SDL_SCANCODE_6: // Diffuse only
                flags = PBRPushConstants::FLAG_DEBUG_DIFFUSE_ONLY;
                LOG_INFO("Debug", "Showing DIFFUSE contribution only");
                break;
            case SDL_SCANCODE_7: // Specular only
                flags = PBRPushConstants::FLAG_DEBUG_SPECULAR_ONLY;
                LOG_INFO("Debug", "Showing SPECULAR contribution only");
                break;
            case SDL_SCANCODE_8: // F0 (reflectivity)
                flags = PBRPushConstants::FLAG_DEBUG_F0;
                LOG_INFO("Debug", "Showing F0 (reflectivity)");
                break;
            case SDL_SCANCODE_9: // Albedo after baseColorFactor
                flags = (1u << 16);
                LOG_INFO("Debug", "Showing ALBEDO * baseColorFactor");
                break;
            case SDL_SCANCODE_B: // baseColorFactor itself
                flags = (1u << 17);
                LOG_INFO("Debug", "Showing baseColorFactor UBO value");
                break;
            case SDL_SCANCODE_T: // Toggle tonemapping
                flags ^= PBRPushConstants::FLAG_DISABLE_TONEMAPPING;
                LOG_INFO("Debug", "Tonemapping: %s", 
                        (flags & PBRPushConstants::FLAG_DISABLE_TONEMAPPING) ? "OFF" : "ON");
                break;
            case SDL_SCANCODE_I: // Toggle IBL
                flags ^= PBRPushConstants::FLAG_DISABLE_IBL;
                LOG_INFO("Debug", "IBL: %s",
                        (flags & PBRPushConstants::FLAG_DISABLE_IBL) ? "OFF" : "ON");
                break;
            case SDL_SCANCODE_N: // Toggle normal mapping
                flags ^= PBRPushConstants::FLAG_DISABLE_NORMAL_MAPPING;
                LOG_INFO("Debug", "Normal mapping: %s",
                        (flags & PBRPushConstants::FLAG_DISABLE_NORMAL_MAPPING) ? "OFF" : "ON");
                break;
            case SDL_SCANCODE_L: // Toggle direct light
                flags ^= PBRPushConstants::FLAG_DISABLE_DIRECT_LIGHT;
                LOG_INFO("Debug", "Direct light: %s",
                        (flags & PBRPushConstants::FLAG_DISABLE_DIRECT_LIGHT) ? "OFF" : "ON");
                break;
            case SDL_SCANCODE_H: // Print help
                printHelp();
                changed = false;
                break;
            default:
                changed = false;
                break;
        }
        
        if (changed) {
            setPBRDebugFlags(flags);
        }
    }

    void printHelp() {
        LOG_INFO("Debug", "========================================");
        LOG_INFO("Debug", "  Debug Controls:");
        LOG_INFO("Debug", "========================================");
        LOG_INFO("Debug", "  0 = Normal rendering");
        LOG_INFO("Debug", "  1 = Albedo texture");
        LOG_INFO("Debug", "  2 = Normals");
        LOG_INFO("Debug", "  3 = Metallic");
        LOG_INFO("Debug", "  4 = Roughness");
        LOG_INFO("Debug", "  5 = Ambient Occlusion");
        LOG_INFO("Debug", "  6 = Diffuse contribution only");
        LOG_INFO("Debug", "  7 = Specular contribution only");
        LOG_INFO("Debug", "  8 = F0 (reflectivity)");
        LOG_INFO("Debug", "  T = Toggle tonemapping");
        LOG_INFO("Debug", "  I = Toggle IBL");
        LOG_INFO("Debug", "  N = Toggle normal mapping");
        LOG_INFO("Debug", "  L = Toggle direct lighting");
        LOG_INFO("Debug", "  H = This help");
        LOG_INFO("Debug", "========================================");
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
