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
#include <voxel/voxel_mesher.h>
#include <voxel/mesh_buffer_pool.h>

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
        , cameraPos_(0.0f, 150.0f, 0.0f)  // Start above origin looking down at terrain
        , cameraYaw_(0.0f)   // Looking forward (+Z direction)
        , cameraPitch_(-0.6f)  // Looking down at terrain
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
            2000.0f  // Very far view distance for LOD
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
        treeConfig.chunkSize = 32;
        treeConfig.maxLevels = 5;  // 0-5 = 6 levels (max 32*32=1024 unit chunks)
        treeConfig.maxNodes = 4096;
        treeConfig.maxJobsPerFrame = 16;

        visTree_.initialize(treeConfig);
        LOG_INFO("VoxelLOD", "VisTree initialized (%d levels, threshold %.0f px)",
                 treeConfig.maxLevels, treeConfig.screenSpaceThreshold);

        // Initialize mesh buffer pool
        meshBufferPool_ = std::make_unique<MeshBufferPool>();
        meshBufferPool_->initialize(16);

        // Initialize mesher
        mesher_ = std::make_unique<VoxelMesher>();
        mesher_->initialize();

        // Allocate voxel data buffer
        voxelData_ = std::make_unique<uint8_t[]>(PADDED_SIZE * PADDED_SIZE * PADDED_SIZE);

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

        // Process geometry generation jobs
        auto& jobs = visTree_.getGeomGenJobs();
        int jobsProcessed = 0;
        const int maxJobsPerFrame = 8;  // Process more jobs per frame

        // Sort jobs by distance to camera (closest first) - do this periodically
        static int sortCounter = 0;
        if (!jobs.empty() && ++sortCounter % 30 == 0) {
            std::sort(jobs.begin(), jobs.end(),
                [camX = cameraPos_.x, camZ = cameraPos_.z](const GeomGenJob& a, const GeomGenJob& b) {
                    float aCx = (a.bounds.x0 + a.bounds.x1) * 0.5f;
                    float aCz = (a.bounds.z0 + a.bounds.z1) * 0.5f;
                    float bCx = (b.bounds.x0 + b.bounds.x1) * 0.5f;
                    float bCz = (b.bounds.z0 + b.bounds.z1) * 0.5f;
                    float distA = (aCx - camX) * (aCx - camX) + (aCz - camZ) * (aCz - camZ);
                    float distB = (bCx - camX) * (bCx - camX) + (bCz - camZ) * (bCz - camZ);
                    return distA > distB;  // Sort descending so closest are at back (pop_back)
                });
        }

        // Debug: log job quadrant distribution once
        static bool jobsLogged = false;
        if (!jobsLogged && jobs.size() > 4) {
            int negNeg = 0, negPos = 0, posNeg = 0, posPos = 0;
            for (const auto& j : jobs) {
                float cx = (j.bounds.x0 + j.bounds.x1) * 0.5f;
                float cz = (j.bounds.z0 + j.bounds.z1) * 0.5f;
                if (cx < 0 && cz < 0) negNeg++;
                else if (cx < 0 && cz >= 0) negPos++;
                else if (cx >= 0 && cz < 0) posNeg++;
                else posPos++;
            }
            LOG_INFO("VoxelLOD", "Job quadrants: --=%d, -+=%d, +-=%d, ++=%d (total %zu)",
                     negNeg, negPos, posNeg, posPos, jobs.size());
            jobsLogged = true;
        }

        while (jobsProcessed < maxJobsPerFrame && !jobs.empty()) {
            GeomGenJob job = jobs.back();
            jobs.pop_back();

            // Generate voxel data for this LOD level
            generateLODTerrain(job.bounds, job.level);

            // Mesh the voxel data
            MeshBuffer* buffer = meshBufferPool_->acquire();
            if (!buffer) continue;

            mesher_->setBuffer(buffer);

            // Create temp chunk data from our voxel buffer
            ChunkVoxelData tempChunk;
            std::memcpy(tempChunk.blocks, voxelData_.get(),
                       PADDED_SIZE * PADDED_SIZE * PADDED_SIZE);

            ChunkCoord dummyCoord{0, 0, 0};
            const ChunkVoxelData* neighbors[6] = {nullptr};
            mesher_->beginChunk(&tempChunk, neighbors, dummyCoord);

            MeshResult result = mesher_->meshify();

            meshBufferPool_->release(buffer);

            if (result.numVertices > 0) {
                // Find free GPU slot
                uint32_t gpuSlot = UINT32_MAX;
                for (uint32_t i = 0; i < PipelineVoxel::MAX_CHUNKS; ++i) {
                    if (!slotUsed_[i]) {
                        gpuSlot = i;
                        slotUsed_[i] = true;
                        break;
                    }
                }

                if (gpuSlot != UINT32_MAX) {
                    // Upload to GPU
                    const void* vertices = mesher_->getStbVertexBuffer();
                    size_t dataSize = result.numVertices * 8;

                    bool uploaded = voxelPipeline_->uploadChunkMesh(
                        gpuSlot,
                        vertices,
                        dataSize,
                        job.translate,
                        job.scale
                    );

                    if (uploaded) {
                        visTree_.applyGeom(job.nodeIndex, static_cast<int16_t>(gpuSlot),
                                          result.numVertices, false);
                        totalVertices_ += result.numVertices;
                    }
                }
            }
            else {
                // Empty volume
                visTree_.applyGeom(job.nodeIndex, -1, 0, true);
            }

            jobsProcessed++;
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
        cameraUBO.nearFarFov = glm::vec4(0.5f, 2000.0f, PI / 3.0f,
                                         static_cast<float>(getWidth()) / getHeight());

        uint32_t frameIndex = renderer->getCurrentFrameIndex();
        voxelPipeline_->setCameraUBO(cameraUBO, frameIndex);

        // Render
        VkCommandBuffer cmd = renderer->getCurrentCommandBuffer();
        voxelPipeline_->fillCommandBuffer(cmd, frameIndex);

        // Log stats periodically
        if (frameCount_ % 120 == 0) {
            LOG_DEBUG("VoxelLOD", "Draw: %d nodes, Pending: %d jobs, Vertices: %u",
                     visTree_.getDrawCount(), visTree_.getPendingJobCount(), totalVertices_);
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
        visTree_.shutdown();
        mesher_->shutdown();
        meshBufferPool_->shutdown();
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

    /**
     * @brief Generate terrain for a LOD region
     *
     * @param bounds World bounds for this node
     * @param level LOD level (0 = most detailed)
     */
    void generateLODTerrain(const VisBounds& bounds, int level) {
        // Clear voxel data
        std::memset(voxelData_.get(), 0, PADDED_SIZE * PADDED_SIZE * PADDED_SIZE);

        // Calculate world-to-voxel scale
        const float boundsWidth = static_cast<float>(bounds.x1 - bounds.x0);
        const float boundsDepth = static_cast<float>(bounds.z1 - bounds.z0);
        const float voxelSizeX = boundsWidth / CHUNK_SIZE;
        const float voxelSizeZ = boundsDepth / CHUNK_SIZE;

        const float noiseScale = 0.01f;
        const float amplitude = 40.0f;
        const float baseHeight = 32.0f;

        // Generate terrain using simplex noise
        for (int lx = 0; lx < PADDED_SIZE; ++lx) {
            for (int lz = 0; lz < PADDED_SIZE; ++lz) {
                // World coordinates
                float wx = bounds.x0 + (lx - CHUNK_BORDER) * voxelSizeX;
                float wz = bounds.z0 + (lz - CHUNK_BORDER) * voxelSizeZ;

                // Multi-octave simplex noise
                glm::vec2 p(wx * noiseScale, wz * noiseScale);
                float n = glm::simplex(p * 0.5f) * 1.0f;
                n += glm::simplex(p * 2.0f) * 0.5f;
                n += glm::simplex(p * 4.0f) * 0.25f;
                n = n * 0.5f + 0.5f;  // Normalize to 0-1

                int height = static_cast<int>(baseHeight + n * amplitude);
                height = std::clamp(height, 0, static_cast<int>(CHUNK_SIZE) - 1);

                // Fill column
                for (int ly = 0; ly < PADDED_SIZE; ++ly) {
                    int worldY = ly - CHUNK_BORDER;

                    uint8_t block = 0;  // Air
                    if (worldY < height - 3) {
                        block = 1;  // Stone
                    } else if (worldY < height - 1) {
                        block = 2;  // Dirt
                    } else if (worldY < height) {
                        block = 3;  // Grass
                    }

                    // Index: z varies fastest (stride 1), then y (stride 18), then x (stride 324)
                    int idx = lx * PADDED_SIZE * PADDED_SIZE + ly * PADDED_SIZE + lz;
                    voxelData_[idx] = block;
                }
            }
        }
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
    std::unique_ptr<VoxelMesher> mesher_;
    std::unique_ptr<MeshBufferPool> meshBufferPool_;
    std::unique_ptr<uint8_t[]> voxelData_;

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
