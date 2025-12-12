/**
 * @file voxel_visual_main.cpp
 * @brief Visual Voxel Demo - Infinite procedural world with lighting
 *
 * Demonstrates:
 * - Infinite voxel world generation
 * - Real-time chunk streaming
 * - PBR-style lighting on voxel terrain
 * - Fly camera controls
 */

#include "rendering/application.h"
#include "rendering/pipeline_voxel.h"
#include "rendering/camera.h"
#include "logging/logging.h"
#include "math/math.h"
#include "platform/platform.h"

#include <voxel/voxel.h>

#include "vulkan_backend.h"

#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cmath>

using namespace jupiter::rendering;
using namespace jupiter::voxel;
using namespace jupiter::math;
using namespace jupiter::logging;

/**
 * @brief Tracks uploaded chunk GPU state
 */
struct UploadedChunk {
    uint32_t chunkIndex;       // Index in PipelineVoxel
    glm::vec3 worldOffset;
    glm::vec3 scale;
    uint32_t vertexCount;
};

/**
 * @brief Visual Voxel Demo - Infinite World
 */
class VoxelVisualDemo : public Application {
public:
    VoxelVisualDemo()
        : Application("Voxel World - Infinite Terrain", 1920, 1080, false)
        , cameraPos_(0.0f, 64.0f, 0.0f)  // Start above ground
        , cameraYaw_(0.0f)
        , cameraPitch_(-0.3f)  // Looking slightly down
    {
    }

protected:
    void onInit() override {
        LOG_INFO("VoxelDemo", "========================================");
        LOG_INFO("VoxelDemo", "  Infinite Voxel World Demo");
        LOG_INFO("VoxelDemo", "========================================");

        // Ensure relative asset/shader paths resolve correctly regardless of launch CWD.
        // Our build copies SPIR-V to: <exe_dir>/shaders/...
        if (const char* basePath = SDL_GetBasePath()) {
            if (jupiter::platform::FileSystem::setCurrentWorkingDirectory(basePath)) {
                LOG_INFO("VoxelDemo", "Working directory set to executable dir: %s", basePath);
            } else {
                LOG_WARN("VoxelDemo", "Failed to set working directory to: %s", basePath);
            }
        } else {
            LOG_WARN("VoxelDemo", "SDL_GetBasePath() failed; shader loads may depend on launch CWD");
        }

        // Create fly camera with far view distance
        camera_ = createPerspectiveCamera(
            PI / 3.0f,  // 60 degree FOV for immersion
            static_cast<float>(getWidth()) / getHeight(),
            0.5f,
            1000.0f  // Far plane for distant terrain
        );
        setActiveCamera(camera_);
        updateCameraTransform();

        LOG_INFO("VoxelDemo", "Camera initialized at height %.0f", cameraPos_.y);

        // Get renderer
        auto* renderer = getRenderer();
        if (!renderer) {
            LOG_ERROR("VoxelDemo", "Renderer not available!");
            return;
        }

        // Create voxel pipeline
        PipelineConfig pipelineConfig;
        pipelineConfig.name = "VoxelPipeline";
        pipelineConfig.type = PipelineType::GraphicsOnScreen;
        pipelineConfig.viewportWidth = getWidth();
        pipelineConfig.viewportHeight = getHeight();

        voxelPipeline_ = std::make_unique<PipelineVoxel>(
            renderer->getDevice(),
            renderer->getPhysicalDevice(),
            pipelineConfig,
            renderer->getRenderPass(),
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_D32_SFLOAT
        );

        // Set lighting - warm sunlight with blue ambient
        VoxelLightUBO light;
        // Sun direction (afternoon sun from the west)
        light.sunDirection = glm::vec4(glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f)), 3.0f);
        // Warm golden sunlight
        light.sunColor = glm::vec4(1.0f, 0.95f, 0.85f, 1.0f);
        // Blue sky ambient
        light.ambientColor = glm::vec4(0.3f, 0.35f, 0.45f, 1.0f);
        voxelPipeline_->setLightUBO(light);

        LOG_INFO("VoxelDemo", "Pipeline created with lighting");

        // Initialize voxel world with larger view distance
        VoxelWorldConfig config;
        config.viewDistance = 12;  // Horizontal chunk view distance (XZ)
        config.maxChunks = 4096;   // (Currently not enforced by VoxelWorld, but keep consistent)
        config.seed = 42;          // Fixed seed for reproducible terrain
        config.meshingBudgetPercent = 0.40f;  // 40% of frame time for meshing (faster loading)

        voxelWorld_ = std::make_unique<VoxelWorld>();
        if (!voxelWorld_->initialize(config)) {
            LOG_ERROR("VoxelDemo", "Failed to initialize VoxelWorld!");
            return;
        }

        // Set RAW mesh callback for direct GPU upload
        voxelWorld_->setRawMeshCallback([this](const ChunkCoord& coord,
                                               uint32_t poolIndex,
                                               const void* stbVertices,
                                               uint32_t vertexCount,
                                               const glm::vec3& scale,
                                               const glm::vec3& aabbMin,
                                               const glm::vec3& aabbMax) {
            onChunkMeshedRaw(coord, poolIndex, stbVertices, vertexCount, scale, aabbMin, aabbMax);
        });

        // Set unload callback
        voxelWorld_->setUnloadCallback([this](const ChunkCoord& coord, uint32_t poolIndex) {
            onChunkUnloaded(coord, poolIndex);
        });

        LOG_INFO("VoxelDemo", "VoxelWorld initialized (view distance: %d chunks)", config.viewDistance);

        // Load initial chunks around camera
        LOG_INFO("VoxelDemo", "Loading initial chunks...");
        for (int i = 0; i < 200; ++i) {
            voxelWorld_->update(glm::vec3(cameraPos_.x, cameraPos_.y, cameraPos_.z), 0.016f);
        }

        LOG_INFO("VoxelDemo", "Initial load complete: %zu chunks, %u vertices uploaded",
                 uploadedChunks_.size(), totalVertices_);

        LOG_INFO("VoxelDemo", "");
        LOG_INFO("VoxelDemo", "Controls:");
        LOG_INFO("VoxelDemo", "  WASD      - Move horizontally");
        LOG_INFO("VoxelDemo", "  Space     - Move up");
        LOG_INFO("VoxelDemo", "  Ctrl      - Move down");
        LOG_INFO("VoxelDemo", "  Shift     - Move faster");
        LOG_INFO("VoxelDemo", "  Mouse     - Look around (click to grab)");
        LOG_INFO("VoxelDemo", "  Escape    - Release mouse / Exit");
        LOG_INFO("VoxelDemo", "  F         - Toggle fog");
        LOG_INFO("VoxelDemo", "");

        initialized_ = true;
    }

    void onUpdate(float deltaTime) override {
        if (!initialized_) return;

        // Update voxel world - streams chunks based on camera position
        voxelWorld_->update(glm::vec3(cameraPos_.x, cameraPos_.y, cameraPos_.z), deltaTime);

        // Update stats display periodically
        frameCount_++;
        if (frameCount_ % 60 == 0) {
            uint32_t loaded = voxelWorld_->getLoadedChunkCount();
            uint32_t pending = voxelWorld_->getPendingMeshCount();
            // Could display HUD here
        }
    }

    void onRender() override {
        if (!initialized_ || !voxelPipeline_) return;

        auto* renderer = getRenderer();
        if (!renderer) return;

        // Update camera UBO
        CameraUBO cameraUBO;
        cameraUBO.view = camera_->getViewMatrix().get();
        cameraUBO.projection = camera_->getProjectionMatrix().get();
        cameraUBO.viewProjection = cameraUBO.projection * cameraUBO.view;
        cameraUBO.cameraPosition = glm::vec4(cameraPos_.x, cameraPos_.y, cameraPos_.z, 1.0f);
        cameraUBO.nearFarFov = glm::vec4(0.5f, 1000.0f, PI / 3.0f, static_cast<float>(getWidth()) / getHeight());

        uint32_t frameIndex = renderer->getCurrentFrameIndex();
        voxelPipeline_->setCameraUBO(cameraUBO, frameIndex);

        // Render all voxel chunks
        VkCommandBuffer cmd = renderer->getCurrentCommandBuffer();
        voxelPipeline_->fillCommandBuffer(cmd, frameIndex);
    }

    void onInput(float deltaTime) override {
        if (!initialized_) return;

        // Poll SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                requestClose();
                return;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                handleKeyDown(event.key.scancode, event.key.windowID);
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseGrabbed_) {
                float sensitivity = 0.002f;
                cameraYaw_ -= event.motion.xrel * sensitivity;
                cameraPitch_ -= event.motion.yrel * sensitivity;
                cameraPitch_ = std::clamp(cameraPitch_, -1.5f, 1.5f);
                updateCameraTransform();
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_LEFT && !mouseGrabbed_) {
                    SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromID(event.button.windowID), true);
                    mouseGrabbed_ = true;
                }
            }
        }

        // Continuous keyboard movement
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float speed = 30.0f * deltaTime;  // Base speed: 30 units/sec

        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            speed *= 4.0f;  // Sprint: 120 units/sec
        }

        // Calculate movement vectors from camera orientation
        glm::vec3 forward(
            std::sin(cameraYaw_),
            0.0f,  // Don't pitch forward movement
            std::cos(cameraYaw_)
        );
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

        if (keys[SDL_SCANCODE_W]) cameraPos_ += forward * speed;
        if (keys[SDL_SCANCODE_S]) cameraPos_ -= forward * speed;
        if (keys[SDL_SCANCODE_A]) cameraPos_ -= right * speed;
        if (keys[SDL_SCANCODE_D]) cameraPos_ += right * speed;
        if (keys[SDL_SCANCODE_SPACE]) cameraPos_.y += speed;
        if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) cameraPos_.y -= speed;

        // Keep camera above ground (simple floor)
        if (cameraPos_.y < 5.0f) cameraPos_.y = 5.0f;

        updateCameraTransform();
    }

    void handleKeyDown(SDL_Scancode scancode, uint32_t windowID) {
        switch (scancode) {
            case SDL_SCANCODE_ESCAPE:
                if (mouseGrabbed_) {
                    SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromID(windowID), false);
                    mouseGrabbed_ = false;
                } else {
                    requestClose();
                }
                break;

            case SDL_SCANCODE_F:
                // Toggle fog (future feature)
                fogEnabled_ = !fogEnabled_;
                LOG_INFO("VoxelDemo", "Fog: %s", fogEnabled_ ? "ON" : "OFF");
                break;

            case SDL_SCANCODE_G:
                // Teleport to ground level at current XZ
                cameraPos_.y = 64.0f;
                LOG_INFO("VoxelDemo", "Teleported to height 64");
                break;

            default:
                break;
        }
    }

    void onShutdown() override {
        LOG_INFO("VoxelDemo", "Shutting down...");
        LOG_INFO("VoxelDemo", "Final stats: %zu chunks uploaded, %u total vertices",
                 uploadedChunks_.size(), totalVertices_);

        uploadedChunks_.clear();
        if (voxelWorld_) {
            voxelWorld_->shutdown();
        }
        voxelPipeline_.reset();

        LOG_INFO("VoxelDemo", "Shutdown complete");
    }

private:
    void updateCameraTransform() {
        if (!camera_) return;

        glm::vec3 forward(
            std::sin(cameraYaw_) * std::cos(cameraPitch_),
            std::sin(cameraPitch_),
            std::cos(cameraYaw_) * std::cos(cameraPitch_)
        );
        glm::vec3 target = cameraPos_ + forward;
        camera_->lookAt(
            Vector3(cameraPos_.x, cameraPos_.y, cameraPos_.z),
            Vector3(target.x, target.y, target.z),
            Vector3::up()
        );
    }

    void onChunkMeshedRaw(const ChunkCoord& coord,
                          uint32_t poolIndex,
                          const void* stbVertices,
                          uint32_t vertexCount,
                          const glm::vec3& scale,
                          const glm::vec3& aabbMin,
                          const glm::vec3& aabbMax) {
        if (!voxelPipeline_ || vertexCount == 0) return;

        // Calculate world offset for this chunk
        glm::vec3 worldOffset = coord.toWorldPos();

        uint64_t coordKey = coord.hash();

        // Check if we already have this chunk uploaded
        auto it = uploadedChunks_.find(coordKey);
        uint32_t chunkIndex;

        if (it != uploadedChunks_.end()) {
            // Re-upload to same slot (chunk was edited)
            chunkIndex = it->second.chunkIndex;
        } else {
            // Find a free slot starting from hint
            chunkIndex = UINT32_MAX;
            for (uint32_t i = 0; i < PipelineVoxel::MAX_CHUNKS; ++i) {
                uint32_t slot = (nextSlot_ + i) % PipelineVoxel::MAX_CHUNKS;
                if (!slotUsed_[slot]) {
                    chunkIndex = slot;
                    nextSlot_ = (slot + 1) % PipelineVoxel::MAX_CHUNKS;
                    break;
                }
            }
            if (chunkIndex == UINT32_MAX) {
                // No free slots - skip this chunk
                LOG_DEBUG("VoxelDemo", "No free chunk slots, skipping chunk (%d,%d,%d)",
                          coord.x, coord.y, coord.z);
                return;
            }
        }

        // Upload raw 8-byte stb_voxel_render vertices directly
        size_t dataSize = vertexCount * 8;  // 8 bytes per VoxelVertexGPU

        bool uploaded = voxelPipeline_->uploadChunkMesh(
            chunkIndex,
            stbVertices,
            dataSize,
            worldOffset,
            scale
        );

        if (uploaded) {
            // Track uploaded chunk
            UploadedChunk chunk;
            chunk.chunkIndex = chunkIndex;
            chunk.worldOffset = worldOffset;
            chunk.scale = scale;
            chunk.vertexCount = vertexCount;
            uploadedChunks_[coordKey] = chunk;
            slotUsed_[chunkIndex] = true;

            totalVertices_ += vertexCount;

            // Log occasionally
            if (uploadedChunks_.size() % 10 == 0) {
                LOG_DEBUG("VoxelDemo", "Uploaded chunk (%d,%d,%d): %u verts, total chunks: %zu",
                          coord.x, coord.y, coord.z, vertexCount, uploadedChunks_.size());
            }
        }
    }

    void onChunkUnloaded(const ChunkCoord& coord, uint32_t poolIndex) {
        uint64_t coordKey = coord.hash();
        auto it = uploadedChunks_.find(coordKey);
        if (it != uploadedChunks_.end()) {
            uint32_t freedSlot = it->second.chunkIndex;
            totalVertices_ -= it->second.vertexCount;
            slotUsed_[freedSlot] = false;  // Free the slot
            uploadedChunks_.erase(it);

            // Log unloads occasionally
            static uint32_t unloadCount = 0;
            if (++unloadCount % 10 == 0) {
                LOG_DEBUG("VoxelDemo", "Unloaded chunk (%d,%d,%d), freed slot %u, total chunks: %zu",
                          coord.x, coord.y, coord.z, freedSlot, uploadedChunks_.size());
            }
        }
    }

    // Camera state
    PerspectiveCamera* camera_ = nullptr;
    glm::vec3 cameraPos_;
    float cameraYaw_;
    float cameraPitch_;
    bool mouseGrabbed_ = false;

    // Voxel systems
    std::unique_ptr<VoxelWorld> voxelWorld_;
    std::unique_ptr<PipelineVoxel> voxelPipeline_;

    // Uploaded chunk tracking
    std::unordered_map<uint64_t, UploadedChunk> uploadedChunks_;
    std::array<bool, PipelineVoxel::MAX_CHUNKS> slotUsed_{};  // Simple array for slot tracking
    uint32_t nextSlot_ = 0;  // Hint for next free slot search

    // Stats
    uint32_t totalVertices_ = 0;
    uint32_t frameCount_ = 0;

    // Settings
    bool fogEnabled_ = true;
    bool initialized_ = false;
};

int main(int argc, char* argv[]) {
    LOG_INFO("VoxelDemo", "========================================");
    LOG_INFO("VoxelDemo", "  Starting Infinite Voxel World...");
    LOG_INFO("VoxelDemo", "========================================");

    VoxelVisualDemo demo;
    return demo.run();
}
