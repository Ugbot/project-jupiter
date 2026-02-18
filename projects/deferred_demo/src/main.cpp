#include "rendering/application.h"
#include "rendering/pbr_push_constants.h"
#include "logging/logging.h"
#include "math/math.h"
#include <cmath>
#include <SDL3/SDL.h>

using namespace jupiter::rendering;
using namespace jupiter::math;

/**
 * @brief Deferred Rendering Demo
 *
 * Demonstrates:
 * - Deferred rendering pipeline (G-buffer pass + lighting pass)
 * - Multiple objects with PBR materials
 * - Multiple dynamic lights (showcasing deferred's efficiency)
 * - Toggle between forward and deferred rendering for comparison
 * - Debug visualizations for G-buffer contents
 */
class DeferredDemoApplication : public Application {
public:
    DeferredDemoApplication()
        : Application("Deferred Rendering Demo - Project Jupiter", 1280, 720, false)
        , cameraAngle_(0.0f)
        , cameraRadius_(8.0f)
        , cameraHeight_(3.0f)
        , animationTime_(0.0f)
        , rotatingLightsEnabled_(true) {
    }

protected:
    void onInit() override {
        LOG_INFO("DeferredDemo", "========================================");
        LOG_INFO("DeferredDemo", "  Initializing Deferred Rendering Demo");
        LOG_INFO("DeferredDemo", "========================================");

        // Enable automatic PBR rendering
        enableAutoRender();

        // Enable deferred rendering mode
        if (!enableDeferredRendering(true)) {
            LOG_ERROR("DeferredDemo", "Failed to enable deferred rendering!");
            LOG_WARN("DeferredDemo", "Falling back to forward rendering...");
        } else {
            LOG_INFO("DeferredDemo", "✓ Deferred rendering enabled");
        }

        // Disable IBL to avoid stalling on some GPUs during generation
        // Users can enable via env var if desired
        const char* iblEnv = std::getenv("JUPITER_DEFERRED_DEMO_IBL");
        if (iblEnv && std::strcmp(iblEnv, "1") == 0) {
            if (loadIBLFromHDR("Assets/Textures/piazza_bologni_1k.hdr")) {
                LOG_INFO("DeferredDemo", "✓ Loaded HDR environment map for IBL");
            } else {
                LOG_WARN("DeferredDemo", "Using fallback IBL");
                setIBLEnabled(false);
            }
        } else {
            LOG_INFO("DeferredDemo", "IBL disabled (set JUPITER_DEFERRED_DEMO_IBL=1 to enable)");
            setIBLEnabled(false);
            auto& settings = getRenderSettingsMutable();
            settings.maxReflectionLodClamp = 0.0f;
            applyRenderSettings(settings);
        }

        // Create perspective camera
        auto* camera = createPerspectiveCamera(
            PI / 4.0f,
            static_cast<float>(getWidth()) / static_cast<float>(getHeight()),
            0.1f,
            100.0f
        );

        Vector3 cameraPos(0.0f, cameraHeight_, cameraRadius_);
        camera->lookAt(cameraPos, Vector3::zero(), Vector3::up());
        setActiveCamera(camera);
        LOG_INFO("DeferredDemo", "✓ Created perspective camera");

        // Load GLTF model (DamagedHelmet)
        LOG_INFO("DeferredDemo", "Loading models...");
        auto helmetHandles = loadModel("models/DamagedHelmet/DamagedHelmet.gltf");
        if (helmetHandles.empty()) {
            LOG_ERROR("DeferredDemo", "Failed to load DamagedHelmet model!");
        } else {
            LOG_INFO("DeferredDemo", "✓ Loaded DamagedHelmet with {} meshes", helmetHandles.size());
            helmetHandle_ = helmetHandles[0];
        }

        // Spawn additional primitive objects to demonstrate deferred handling many meshes
        float white[3] = {1.0f, 1.0f, 1.0f};
        float red[3] = {0.9f, 0.2f, 0.1f};
        float green[3] = {0.1f, 0.8f, 0.2f};
        float blue[3] = {0.1f, 0.3f, 0.9f};
        float gold[3] = {1.0f, 0.84f, 0.0f};
        float silver[3] = {0.75f, 0.75f, 0.75f};

        // Ground plane
        float groundPos[3] = {0.0f, -1.5f, 0.0f};
        spawnPlane(groundPos, 20.0f, 20.0f, silver);
        LOG_INFO("DeferredDemo", "✓ Spawned ground plane");

        // Sphere grid to demonstrate many objects
        const int gridSize = 3;
        const float spacing = 2.5f;
        float colors[6][3] = {
            {0.9f, 0.2f, 0.1f},  // red
            {0.1f, 0.8f, 0.2f},  // green
            {0.1f, 0.3f, 0.9f},  // blue
            {1.0f, 0.84f, 0.0f}, // gold
            {0.8f, 0.2f, 0.8f},  // purple
            {0.2f, 0.8f, 0.8f}   // cyan
        };

        int colorIdx = 0;
        for (int x = -gridSize; x <= gridSize; ++x) {
            for (int z = -gridSize; z <= gridSize; ++z) {
                if (x == 0 && z == 0) continue;  // Skip center (helmet is there)
                
                float pos[3] = {
                    x * spacing,
                    -0.5f,
                    z * spacing
                };
                float* color = colors[colorIdx % 6];
                colorIdx++;
                
                // Alternate between spheres and cubes
                if ((x + z) % 2 == 0) {
                    spawnSphere(pos, 0.5f, 32, 16, color);
                } else {
                    spawnCube(pos, 0.8f, color);
                }
            }
        }
        LOG_INFO("DeferredDemo", "✓ Spawned {} primitive objects", colorIdx);

        // Setup lighting - multiple lights to show deferred efficiency
        setupLights();

        // Print controls
        printControls();

        LOG_INFO("DeferredDemo", "");
        LOG_INFO("DeferredDemo", "========================================");
        LOG_INFO("DeferredDemo", "  Deferred Demo Ready!");
        LOG_INFO("DeferredDemo", "  Rendering Mode: %s", isDeferredRenderingEnabled() ? "DEFERRED" : "FORWARD");
        LOG_INFO("DeferredDemo", "========================================");
    }

    void setupLights() {
        // Primary directional light (sun)
        float sunDir[3] = {-0.5f, -1.0f, -0.5f};
        float sunColor[3] = {1.0f, 0.95f, 0.9f};
        addDirectionalLight(sunDir, sunColor, 2.0f);
        LOG_INFO("DeferredDemo", "✓ Added directional light (sun)");

        // Multiple point lights in a circle - this is where deferred shines!
        const int numPointLights = 8;
        float lightRadius = 5.0f;
        
        float lightColors[8][3] = {
            {1.0f, 0.3f, 0.3f},  // Red
            {1.0f, 0.6f, 0.3f},  // Orange
            {1.0f, 1.0f, 0.3f},  // Yellow
            {0.3f, 1.0f, 0.3f},  // Green
            {0.3f, 1.0f, 1.0f},  // Cyan
            {0.3f, 0.3f, 1.0f},  // Blue
            {0.8f, 0.3f, 1.0f},  // Purple
            {1.0f, 0.3f, 0.8f}   // Pink
        };

        for (int i = 0; i < numPointLights; ++i) {
            float angle = (2.0f * PI * i) / numPointLights;
            float x = std::cos(angle) * lightRadius;
            float z = std::sin(angle) * lightRadius;
            float pos[3] = {x, 1.5f, z};
            addPointLight(pos, lightColors[i], 1.5f, 8.0f);
        }
        LOG_INFO("DeferredDemo", "✓ Added {} point lights", numPointLights);

        // Ambient light
        float ambientColor[3] = {0.15f, 0.15f, 0.2f};
        setAmbientLight(ambientColor, 0.4f);
        LOG_INFO("DeferredDemo", "✓ Set ambient light");
    }

    void onUpdate(float deltaTime) override {
        animationTime_ += deltaTime;

        // Rotate camera around the scene
        cameraAngle_ += deltaTime * 0.2f;

        float camX = std::sin(cameraAngle_) * cameraRadius_;
        float camZ = std::cos(cameraAngle_) * cameraRadius_;

        auto* camera = getActiveCamera();
        if (camera) {
            Vector3 cameraPos(camX, cameraHeight_, camZ);
            camera->lookAt(cameraPos, Vector3::zero(), Vector3::up());
        }

        // Animate point lights if enabled
        if (rotatingLightsEnabled_) {
            // Point lights are updated via LightManager
            // For now, lights are static but could be animated
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
        switch (scancode) {
            case SDL_SCANCODE_SPACE:
                // Toggle deferred/forward rendering
                toggleRenderingMode();
                break;

            case SDL_SCANCODE_R:
                // Toggle rotating lights
                rotatingLightsEnabled_ = !rotatingLightsEnabled_;
                LOG_INFO("DeferredDemo", "Light animation: %s", 
                        rotatingLightsEnabled_ ? "ON" : "OFF");
                break;

            case SDL_SCANCODE_0:
                setPBRDebugFlags(0);
                LOG_INFO("DeferredDemo", "Debug: Normal rendering");
                break;

            case SDL_SCANCODE_1:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_ALBEDO);
                LOG_INFO("DeferredDemo", "Debug: Albedo");
                break;

            case SDL_SCANCODE_2:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_NORMALS);
                LOG_INFO("DeferredDemo", "Debug: Normals");
                break;

            case SDL_SCANCODE_3:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_METALLIC);
                LOG_INFO("DeferredDemo", "Debug: Metallic");
                break;

            case SDL_SCANCODE_4:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_ROUGHNESS);
                LOG_INFO("DeferredDemo", "Debug: Roughness");
                break;

            case SDL_SCANCODE_5:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_AO);
                LOG_INFO("DeferredDemo", "Debug: Ambient Occlusion");
                break;

            case SDL_SCANCODE_6:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_DIFFUSE_ONLY);
                LOG_INFO("DeferredDemo", "Debug: Diffuse contribution only");
                break;

            case SDL_SCANCODE_7:
                setPBRDebugFlags(PBRPushConstants::FLAG_DEBUG_SPECULAR_ONLY);
                LOG_INFO("DeferredDemo", "Debug: Specular contribution only");
                break;

            case SDL_SCANCODE_EQUALS:
            case SDL_SCANCODE_KP_PLUS:
                cameraRadius_ = std::max(3.0f, cameraRadius_ - 0.5f);
                LOG_INFO("DeferredDemo", "Camera distance: %.1f", cameraRadius_);
                break;

            case SDL_SCANCODE_MINUS:
            case SDL_SCANCODE_KP_MINUS:
                cameraRadius_ = std::min(20.0f, cameraRadius_ + 0.5f);
                LOG_INFO("DeferredDemo", "Camera distance: %.1f", cameraRadius_);
                break;

            case SDL_SCANCODE_UP:
                cameraHeight_ = std::min(15.0f, cameraHeight_ + 0.5f);
                LOG_INFO("DeferredDemo", "Camera height: %.1f", cameraHeight_);
                break;

            case SDL_SCANCODE_DOWN:
                cameraHeight_ = std::max(0.5f, cameraHeight_ - 0.5f);
                LOG_INFO("DeferredDemo", "Camera height: %.1f", cameraHeight_);
                break;

            case SDL_SCANCODE_H:
                printControls();
                break;

            case SDL_SCANCODE_ESCAPE:
                requestClose();
                break;

            default:
                break;
        }
    }

    void toggleRenderingMode() {
        bool newMode = !isDeferredRenderingEnabled();
        if (enableDeferredRendering(newMode)) {
            LOG_INFO("DeferredDemo", "========================================");
            LOG_INFO("DeferredDemo", "  Switched to %s rendering", 
                    newMode ? "DEFERRED" : "FORWARD");
            LOG_INFO("DeferredDemo", "========================================");
        } else {
            LOG_ERROR("DeferredDemo", "Failed to switch rendering mode!");
        }
    }

    void printControls() {
        LOG_INFO("DeferredDemo", "");
        LOG_INFO("DeferredDemo", "========================================");
        LOG_INFO("DeferredDemo", "  Controls:");
        LOG_INFO("DeferredDemo", "========================================");
        LOG_INFO("DeferredDemo", "  SPACE     = Toggle Forward/Deferred");
        LOG_INFO("DeferredDemo", "  R         = Toggle light animation");
        LOG_INFO("DeferredDemo", "  +/-       = Zoom in/out");
        LOG_INFO("DeferredDemo", "  UP/DOWN   = Camera height");
        LOG_INFO("DeferredDemo", "  0         = Normal rendering");
        LOG_INFO("DeferredDemo", "  1-7       = Debug visualizations");
        LOG_INFO("DeferredDemo", "              1=Albedo, 2=Normals");
        LOG_INFO("DeferredDemo", "              3=Metallic, 4=Roughness");
        LOG_INFO("DeferredDemo", "              5=AO, 6=Diffuse, 7=Specular");
        LOG_INFO("DeferredDemo", "  H         = Show this help");
        LOG_INFO("DeferredDemo", "  ESC       = Quit");
        LOG_INFO("DeferredDemo", "========================================");
        LOG_INFO("DeferredDemo", "");
    }

private:
    float cameraAngle_;
    float cameraRadius_;
    float cameraHeight_;
    float animationTime_;
    bool rotatingLightsEnabled_;
    RenderableHandle helmetHandle_;
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    LOG_INFO("Main", "");
    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "   Deferred Rendering Demo");
    LOG_INFO("Main", "   Project Jupiter Engine");
    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "");
    LOG_INFO("Main", "This demo showcases the deferred rendering");
    LOG_INFO("Main", "pipeline with multiple objects and lights.");
    LOG_INFO("Main", "");
    LOG_INFO("Main", "Press SPACE to toggle between deferred and");
    LOG_INFO("Main", "forward rendering to compare performance.");
    LOG_INFO("Main", "");

    DeferredDemoApplication app;
    return app.run();
}
