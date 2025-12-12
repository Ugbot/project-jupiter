/**
 * @file voxel_lod_main.cpp
 * @brief LOD Voxel Demo - Infinite world with screen-space error LOD
 *
 * Based on Oryol's StbVoxelDemo VisTree system.
 * Uses quadtree with screen-space error metric for LOD selection.
 */

#include "rendering/application.h"
#include "rendering/pipeline_voxel.h"
#include "rendering/camera.h"
#include "logging/logging.h"
#include "math/math.h"
#include "platform/platform.h"

#include <voxel/vis_tree.h>
#include <voxel/voxel_job_system.h>

#include "vulkan_backend.h"

#include <SDL3/SDL.h>
#include <glm/gtc/noise.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <cmath>

using namespace jupiter::rendering;
using namespace jupiter::voxel;
using namespace jupiter::math;
using namespace jupiter::logging;

/**
 * @brief LOD Voxel Demo with VisTree
 */
class VoxelLODDemo : public Application {
public:
    VoxelLODDemo()
        : Application("Voxel LOD Demo", 1920, 1080, false)
        , cameraPos_(0.0f, 200.0f, 0.0f)  // Start above origin, high enough for tall hills
        , cameraYaw_(0.0f)   // Looking forward (+Z direction)
        , cameraPitch_(-0.3f)  // Slight downward angle to see terrain + horizon
    {
    }

protected:
    void onInit() override {
        LOG_INFO("VoxelLOD", "========================================");
        LOG_INFO("VoxelLOD", "  LOD Voxel World Demo");
        LOG_INFO("VoxelLOD", "========================================");

        // Set working directory
        if (const char* basePath = SDL_GetBasePath()) {
            if (jupiter::platform::FileSystem::setCurrentWorkingDirectory(basePath)) {
                LOG_INFO("VoxelLOD", "Working directory: %s", basePath);
            }
        }

        // Create camera
        camera_ = createPerspectiveCamera(
            PI / 3.0f,
            static_cast<float>(getWidth()) / getHeight(),
            0.5f,
            6000.0f  // Triple draw distance for LOD
        );
        setActiveCamera(camera_);
        updateCameraTransform();

        // Get renderer
        auto* renderer = getRenderer();
        if (!renderer) {
            LOG_ERROR("VoxelLOD", "Renderer not available!");
            return;
        }

        // Create voxel pipeline
        PipelineConfig pipelineConfig;
        pipelineConfig.name = "VoxelLODPipeline";
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

        // Set lighting
        VoxelLightUBO light;
        light.sunDirection = glm::vec4(glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f)), 3.0f);
        light.sunColor = glm::vec4(1.0f, 0.95f, 0.85f, 1.0f);
        light.ambientColor = glm::vec4(0.3f, 0.35f, 0.45f, 1.0f);
        voxelPipeline_->setLightUBO(light);

        // Initialize VisTree
        VisTreeConfig treeConfig;
        treeConfig.displayWidth = getWidth();
        treeConfig.fov = PI / 3.0f;
        treeConfig.screenSpaceThreshold = 8.0f;  // Lower = more detail
        treeConfig.chunkSize = CHUNK_SIZE;  // Must match voxel grid resolution (16)
        treeConfig.maxLevels = 8;  // 0-8 = 9 levels (max 16*256=4096 unit chunks)
        treeConfig.maxNodes = 8192;  // More nodes for larger world
        treeConfig.maxJobsPerFrame = 24;  // Process more jobs

        visTree_.initialize(treeConfig);
        LOG_INFO("VoxelLOD", "VisTree initialized (%d levels, threshold %.0f px)",
                 treeConfig.maxLevels, treeConfig.screenSpaceThreshold);

        // Initialize async job system (replaces single-threaded mesher)
        jobSystem_ = std::make_unique<VoxelJobSystem>();
        if (!jobSystem_->initialize(0, 12345)) {  // 0 = auto-detect worker count
            LOG_ERROR("VoxelLOD", "Failed to initialize job system!");
            return;
        }
        LOG_INFO("VoxelLOD", "Job system initialized with %u worker threads",
                 jobSystem_->getWorkerCount());

        LOG_INFO("VoxelLOD", "");
        LOG_INFO("VoxelLOD", "Controls:");
        LOG_INFO("VoxelLOD", "  WASD      - Move horizontally");
        LOG_INFO("VoxelLOD", "  Space     - Move up");
        LOG_INFO("VoxelLOD", "  Ctrl      - Move down");
        LOG_INFO("VoxelLOD", "  Shift     - Move faster");
        LOG_INFO("VoxelLOD", "  Mouse     - Look around (click to grab)");
        LOG_INFO("VoxelLOD", "  Escape    - Release mouse / Exit");
        LOG_INFO("VoxelLOD", "");

        initialized_ = true;
    }

    void onUpdate(float deltaTime) override {
        if (!initialized_) return;

        // Get view-projection matrix
        glm::mat4 view = camera_->getViewMatrix().get();
        glm::mat4 proj = camera_->getProjectionMatrix().get();
        glm::mat4 viewProj = proj * view;

        // Traverse VisTree
        visTree_.traverse(cameraPos_, viewProj);

        // Recycle freed GPU slots (from merged nodes)
        auto& freedSlots = visTree_.getFreeGeomSlots();
        for (int16_t slot : freedSlots) {
            if (slot >= 0 && slot < static_cast<int16_t>(PipelineVoxel::MAX_CHUNKS)) {
                slotUsed_[slot] = false;
                totalVertices_ -= 1024;  // Approximate vertices freed
            }
        }
        freedSlots.clear();

        // Submit new geometry generation jobs to async workers
        auto& jobs = visTree_.getGeomGenJobs();
        int jobsSubmitted = 0;

        while (!jobs.empty()) {
            GeomGenJob job = jobs.back();
            jobs.pop_back();

            // Find free GPU slot (pre-allocate before submitting to worker)
            uint32_t gpuSlot = UINT32_MAX;
            for (uint32_t i = 0; i < PipelineVoxel::MAX_CHUNKS; ++i) {
                if (!slotUsed_[i]) {
                    gpuSlot = i;
                    slotUsed_[i] = true;  // Reserve slot
                    break;
                }
            }

            if (gpuSlot == UINT32_MAX) {
                // No GPU slots available, put job back
                jobs.push_back(job);
                break;
            }

            // Submit to async job system
            if (jobSystem_->submitJob(job, static_cast<int16_t>(gpuSlot))) {
                jobsSubmitted++;
            } else {
                // Ring buffer full, release slot and put job back
                slotUsed_[gpuSlot] = false;
                jobs.push_back(job);
                break;
            }
        }

        // Poll completed meshes and upload to GPU
        CompletedMesh completed;
        int uploadsThisFrame = 0;
        const int maxUploadsPerFrame = 64;  // Limit uploads per frame to avoid stalls

        while (uploadsThisFrame < maxUploadsPerFrame && jobSystem_->pollCompleted(completed)) {
            if (completed.numVertices > 0 && !completed.meshData.empty()) {
                // Upload to GPU
                bool uploaded = voxelPipeline_->uploadChunkMesh(
                    completed.gpuSlot,
                    completed.meshData.data(),
                    completed.meshData.size(),
                    completed.translate,
                    completed.scale
                );

                if (uploaded) {
                    visTree_.applyGeom(completed.nodeIndex, completed.gpuSlot,
                                      completed.numVertices, false);
                    totalVertices_ += completed.numVertices;
                }
            } else {
                // Empty volume - release pre-allocated GPU slot
                if (completed.gpuSlot >= 0 && completed.gpuSlot < static_cast<int16_t>(PipelineVoxel::MAX_CHUNKS)) {
                    slotUsed_[completed.gpuSlot] = false;
                }
                visTree_.applyGeom(completed.nodeIndex, -1, 0, true);
            }
            uploadsThisFrame++;
        }

        // Update stats
        frameCount_++;
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
        cameraUBO.cameraPosition = glm::vec4(cameraPos_, 1.0f);
        cameraUBO.nearFarFov = glm::vec4(0.5f, 6000.0f, PI / 3.0f,
                                         static_cast<float>(getWidth()) / getHeight());

        uint32_t frameIndex = renderer->getCurrentFrameIndex();
        voxelPipeline_->setCameraUBO(cameraUBO, frameIndex);

        // Render
        VkCommandBuffer cmd = renderer->getCurrentCommandBuffer();
        voxelPipeline_->fillCommandBuffer(cmd, frameIndex);

        // Log stats periodically
        if (frameCount_ % 120 == 0) {
            LOG_DEBUG("VoxelLOD", "Draw: %d, Pending: %llu async + %d tree, Completed: %llu, Vertices: %u",
                     visTree_.getDrawCount(),
                     jobSystem_->getPendingCount(),
                     visTree_.getPendingJobCount(),
                     jobSystem_->getCompletedCount(),
                     totalVertices_);
        }
    }

    void onInput(float deltaTime) override {
        if (!initialized_) return;

        // Poll events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                requestClose();
                return;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    if (mouseGrabbed_) {
                        SDL_SetWindowRelativeMouseMode(
                            SDL_GetWindowFromID(event.key.windowID), false);
                        mouseGrabbed_ = false;
                    } else {
                        requestClose();
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseGrabbed_) {
                float sensitivity = 0.002f;
                cameraYaw_ -= event.motion.xrel * sensitivity;
                cameraPitch_ -= event.motion.yrel * sensitivity;
                cameraPitch_ = std::clamp(cameraPitch_, -1.5f, 1.5f);
                updateCameraTransform();
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (!mouseGrabbed_) {
                    SDL_SetWindowRelativeMouseMode(
                        SDL_GetWindowFromID(event.button.windowID), true);
                    mouseGrabbed_ = true;
                }
            }
        }

        // Movement
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float speed = 50.0f * deltaTime;

        if (keys[SDL_SCANCODE_LSHIFT]) speed *= 4.0f;

        glm::vec3 forward(std::sin(cameraYaw_), 0.0f, std::cos(cameraYaw_));
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

        if (keys[SDL_SCANCODE_W]) cameraPos_ += forward * speed;
        if (keys[SDL_SCANCODE_S]) cameraPos_ -= forward * speed;
        if (keys[SDL_SCANCODE_A]) cameraPos_ -= right * speed;
        if (keys[SDL_SCANCODE_D]) cameraPos_ += right * speed;
        if (keys[SDL_SCANCODE_SPACE]) cameraPos_.y += speed;
        if (keys[SDL_SCANCODE_LCTRL]) cameraPos_.y -= speed;

        if (cameraPos_.y < 5.0f) cameraPos_.y = 5.0f;

        updateCameraTransform();
    }

    void onShutdown() override {
        LOG_INFO("VoxelLOD", "Shutting down...");
        if (jobSystem_) {
            jobSystem_->shutdown();
            LOG_INFO("VoxelLOD", "Job system shutdown complete");
        }
        visTree_.shutdown();
        voxelPipeline_.reset();
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

    // Camera
    PerspectiveCamera* camera_ = nullptr;
    glm::vec3 cameraPos_;
    float cameraYaw_;
    float cameraPitch_;
    bool mouseGrabbed_ = false;

    // Rendering
    std::unique_ptr<PipelineVoxel> voxelPipeline_;

    // LOD system
    VisTree visTree_;
    std::unique_ptr<VoxelJobSystem> jobSystem_;

    // GPU slot tracking
    std::array<bool, PipelineVoxel::MAX_CHUNKS> slotUsed_{};

    // Stats
    uint32_t totalVertices_ = 0;
    uint32_t frameCount_ = 0;
    bool initialized_ = false;
};

int main(int argc, char* argv[]) {
    LOG_INFO("VoxelLOD", "Starting LOD Voxel Demo...");
    VoxelLODDemo demo;
    return demo.run();
}
