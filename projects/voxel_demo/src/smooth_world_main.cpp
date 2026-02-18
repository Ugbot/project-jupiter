/**
 * @file smooth_world_main.cpp
 * @brief Smooth Voxel World Demo - Marching Cubes + Transvoxel terrain with caves
 *
 * Demonstrates:
 * - Smooth terrain using Marching Cubes meshing
 * - 3D Perlin noise for terrain generation
 * - Cave systems using isosurface intersection technique
 * - Transvoxel LOD transitions (when enabled)
 * - VoxelWorldV2 with columnar storage
 *
 * Cave generation based on: https://blog.danol.cz/voxel-cave-generation-using-3d-perlin-noise-isosurfaces/
 */

#include "rendering/application.h"
#include "rendering/pipeline_voxel.h"
#include "rendering/pipeline_smooth_terrain.h"
#include "rendering/pipeline_imgui.h"
#include "rendering/camera.h"
#include "logging/logging.h"
#include "math/math.h"
#include "platform/platform.h"

#include <voxel/voxel.h>
#include <voxel/voxel_world_v2.h>
#include <voxel/perlin_terrain.h>
#include <voxel/mesh_mode.h>
#include <voxel/smooth_vertex.h>
#include <voxel/mesh_optimizer.h>
#include <voxel/voxel_types.h>

#include "vulkan_backend.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_sdl3.h>
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
 * @brief Tracks uploaded smooth mesh GPU state
 */
struct UploadedSmoothChunk {
    uint32_t chunkIndex;
    glm::vec3 worldOffset;
    uint32_t vertexCount;
    uint32_t triangleCount;
};

/**
 * @brief Smooth Voxel World Demo
 */
class SmoothWorldDemo : public Application {
public:
    SmoothWorldDemo()
        : Application("Rolling Hills Demo", 1920, 1080, false)
        , cameraPos_(8.0f, 65.0f, 8.0f)  // Start above rolling hills (base=50, amp=12, max~62)
        , cameraYaw_(0.7f)              // Angled to see terrain
        , cameraPitch_(-0.5f)           // Looking down at terrain
    {
    }

protected:
    void onInit() override {
        LOG_INFO("SmoothDemo", "========================================");
        LOG_INFO("SmoothDemo", "  Rolling Hills Demo");
        LOG_INFO("SmoothDemo", "  Simple Smooth Terrain");
        LOG_INFO("SmoothDemo", "========================================");

        // Set working directory to executable location for shader loading
        if (const char* basePath = SDL_GetBasePath()) {
            if (jupiter::platform::FileSystem::setCurrentWorkingDirectory(basePath)) {
                LOG_INFO("SmoothDemo", "Working directory: %s", basePath);
            }
        }

        // Create fly camera
        camera_ = createPerspectiveCamera(
            PI / 3.0f,  // 60 degree FOV
            static_cast<float>(getWidth()) / getHeight(),
            0.5f,
            1000.0f
        );
        setActiveCamera(camera_);
        updateCameraTransform();

        LOG_INFO("SmoothDemo", "Camera at height %.0f", cameraPos_.y);

        // Get renderer
        auto* renderer = getRenderer();
        if (!renderer) {
            LOG_ERROR("SmoothDemo", "Renderer not available!");
            return;
        }

        // Create smooth terrain pipeline for Marching Cubes output
        smoothPipeline_ = std::make_unique<PipelineSmoothTerrain>();
        if (!smoothPipeline_->initialize(
                renderer->getDevice(),
                renderer->getPhysicalDevice(),
                renderer->getAllocator(),
                renderer->getRenderPass(),
                VK_FORMAT_B8G8R8A8_SRGB,
                VK_FORMAT_D32_SFLOAT)) {
            LOG_ERROR("SmoothDemo", "Failed to create smooth terrain pipeline!");
            return;
        }

        // Set lighting - dramatic afternoon sun
        SmoothTerrainLightUBO light;
        light.sunDirection = glm::vec4(glm::normalize(glm::vec3(-0.5f, -0.7f, -0.4f)), 3.5f);
        light.sunColor = glm::vec4(1.0f, 0.92f, 0.8f, 1.0f);  // Warm golden
        light.ambientColor = glm::vec4(0.25f, 0.30f, 0.40f, 1.0f);  // Cool sky ambient
        smoothPipeline_->setLightUBO(light);

        LOG_INFO("SmoothDemo", "Smooth terrain pipeline created");

        // Initialize ImGui
        imguiPipeline_ = std::make_unique<PipelineImGui>();
        if (!imguiPipeline_->initialize(
                renderer->getDevice(),
                renderer->getPhysicalDevice(),
                renderer->getInstance(),
                renderer->getGraphicsQueue(),
                renderer->getGraphicsQueueFamily(),
                getWindow(),
                renderer->getRenderPass(),
                renderer->getSwapchainFormat(),
                renderer->getSwapchainImageCount()
            )) {
            LOG_ERROR("SmoothDemo", "Failed to initialize ImGui");
        } else {
            LOG_INFO("SmoothDemo", "ImGui initialized");
        }

        // Configure terrain generator for simple rolling hills
        PerlinTerrainConfig terrainConfig;
        terrainConfig.seed = 12345;
        terrainConfig.baseHeight = 50.0f;       // Base height for rolling hills
        terrainConfig.heightAmplitude = 12.0f;  // Gentle rolling hills (smaller amplitude)
        terrainConfig.surfaceFrequency = 0.008f;  // Smooth, rolling hills (lower frequency = larger features)
        terrainConfig.detailFrequency = 0.0f;    // No detail noise - just smooth hills
        terrainConfig.detailAmplitude = 0.0f;    // Disable detail noise
        // Cave settings - disabled for simple hills
        terrainConfig.enableCaves = false;
        terrainConfig.caveFrequency = 0.025f;
        terrainConfig.caveThreshold = 0.08f;
        terrainConfig.caveMinY = 10.0f;
        terrainConfig.caveMaxY = 40.0f;
        terrainConfig.caveSeedA = 3543;
        terrainConfig.caveSeedB = 43264;
        
        terrainGenerator_ = std::make_unique<PerlinTerrainGenerator>(terrainConfig);

        LOG_INFO("SmoothDemo", "Terrain generator configured:");
        LOG_INFO("SmoothDemo", "  Base height: %.0f", terrainConfig.baseHeight);
        LOG_INFO("SmoothDemo", "  Amplitude: %.0f", terrainConfig.heightAmplitude);
        LOG_INFO("SmoothDemo", "  Caves: %s (threshold: %.2f)", 
                 terrainConfig.enableCaves ? "ON" : "OFF", terrainConfig.caveThreshold);

        // Initialize VoxelWorldV2 with SMOOTH mode (Marching Cubes / Transvoxel)
        VoxelWorldV2Config worldConfig;
        worldConfig.viewDistance = 6;           // Smaller view distance for faster startup
        worldConfig.maxChunks = 1024;
        worldConfig.seed = terrainConfig.seed;
        worldConfig.meshingBudgetPercent = 0.50f;  // More meshing budget
        worldConfig.useKernelMeshing = true;
        worldConfig.useColumnarStorage = true;
        worldConfig.meshMode = MeshMode::Smooth;   // Use smooth mode for Transvoxel
        worldConfig.meshConfig.mode = MeshMode::Smooth;
        worldConfig.meshConfig.ambientOcclusion = true;
        worldConfig.meshConfig.isoLevel = 0.0f;

        voxelWorld_ = std::make_unique<VoxelWorldV2>();
        if (!voxelWorld_->initialize(worldConfig)) {
            LOG_ERROR("SmoothDemo", "Failed to initialize VoxelWorldV2!");
            return;
        }

        // Set smooth mesh callback for Marching Cubes output
        voxelWorld_->setSmoothMeshCallback([this](const ChunkCoord& coord,
                                                   const SmoothMeshBuffer& buffer) {
            onSmoothChunkMeshed(coord, buffer);
        });

        voxelWorld_->setUnloadCallback([this](const ChunkCoord& coord, uint32_t poolIndex) {
            onChunkUnloaded(coord, poolIndex);
        });

        // Set custom terrain generator using Perlin noise
        voxelWorld_->setTerrainGenerator([this](ChunkColumns& chunk, const ChunkCoord& coord) {
            terrainGenerator_->generateChunk(chunk, coord);
        });

        LOG_INFO("SmoothDemo", "VoxelWorldV2 initialized with Perlin terrain (view distance: %d)", 
                 worldConfig.viewDistance);

        // Only load a few chunks initially - rest will stream in during gameplay
        LOG_INFO("SmoothDemo", "Loading initial chunks (streaming more during gameplay)...");
        for (int i = 0; i < 10; ++i) {
            voxelWorld_->update(cameraPos_, 0.1f);  // Large dt = more meshing budget
        }

        LOG_INFO("SmoothDemo", "Initial generation complete: %zu chunks, %u verts", 
                 uploadedChunks_.size(), totalVertices_);
        LOG_INFO("SmoothDemo", "");
        LOG_INFO("SmoothDemo", "Controls:");
        LOG_INFO("SmoothDemo", "  WASD       - Move horizontally");
        LOG_INFO("SmoothDemo", "  Space/Ctrl - Move up/down");
        LOG_INFO("SmoothDemo", "  Shift      - Sprint");
        LOG_INFO("SmoothDemo", "  Mouse      - Look (click window to grab)");
        LOG_INFO("SmoothDemo", "  C          - Toggle caves (currently %s)", 
                 terrainConfig.enableCaves ? "ON" : "OFF");
        LOG_INFO("SmoothDemo", "  G          - Teleport to ground");
        LOG_INFO("SmoothDemo", "  Escape     - Release mouse / Exit");
        LOG_INFO("SmoothDemo", "");

        initialized_ = true;
    }

    void onUpdate(float deltaTime) override {
        if (!initialized_) return;

        // Reset upload counter for new frame
        uploadsThisFrame_ = 0;

        // Update voxel world
        voxelWorld_->update(cameraPos_, deltaTime);

        // Update LOD levels for all loaded chunks based on camera distance
        updateChunkLODs();
        
        // Process pending uploads (up to limit per frame)
        processPendingUploads();

        // Update frame stats for ImGui
        frameStats_.update(deltaTime);
        currentFps_ = frameStats_.currentFPS;
        frameCount_++;
    }

    void onRender() override {
        if (!initialized_ || !smoothPipeline_) return;

        auto* renderer = getRenderer();
        if (!renderer) return;

        // Update camera UBO for smooth pipeline
        SmoothTerrainCameraUBO cameraUBO;
        cameraUBO.view = camera_->getViewMatrix().get();
        cameraUBO.projection = camera_->getProjectionMatrix().get();
        cameraUBO.viewProjection = cameraUBO.projection * cameraUBO.view;
        cameraUBO.cameraPosition = glm::vec4(cameraPos_, 1.0f);
        cameraUBO.nearFarFov = glm::vec4(0.5f, 1000.0f, PI / 3.0f, 
                                          static_cast<float>(getWidth()) / getHeight());

        uint32_t frameIndex = renderer->getCurrentFrameIndex();
        smoothPipeline_->setCameraUBO(cameraUBO, frameIndex);

        // Render smooth terrain chunks
        VkCommandBuffer cmd = renderer->getCurrentCommandBuffer();
        smoothPipeline_->fillCommandBuffer(cmd, frameIndex);
        
        // Render ImGui
        if (imguiPipeline_ && imguiPipeline_->isInitialized()) {
            imguiPipeline_->beginFrame();
            
            // Stats window
            if (showStatsWindow_) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);
                ImGui::Begin("Performance", &showStatsWindow_);
                
                ImGui::Text("FPS: %.1f (%.2f ms)", currentFps_, 1000.0f / currentFps_);
                ImGui::Separator();
                
                ImGui::Text("Chunks: %u", static_cast<uint32_t>(uploadedChunks_.size()));
                ImGui::Text("Vertices: %u", totalVertices_);
                ImGui::Text("Triangles: %u", totalVertices_ / 3);
                
                // LOD statistics
                uint32_t lodCounts[4] = {0, 0, 0, 0};
                for (const auto& [coordKey, chunk] : uploadedChunks_) {
                    ChunkCoord coord;
                    coord.x = static_cast<int32_t>(std::floor(chunk.worldOffset.x / CHUNK_SIZE));
                    coord.y = static_cast<int32_t>(std::floor(chunk.worldOffset.y / CHUNK_SIZE));
                    coord.z = static_cast<int32_t>(std::floor(chunk.worldOffset.z / CHUNK_SIZE));
                    LODLevel lod = voxelWorld_->getChunkLOD(coord);
                    lodCounts[static_cast<int>(lod)]++;
                }
                ImGui::Separator();
                ImGui::Text("LOD Distribution:");
                ImGui::BulletText("Full: %u", lodCounts[0]);
                ImGui::BulletText("Half: %u", lodCounts[1]);
                ImGui::BulletText("Quarter: %u", lodCounts[2]);
                ImGui::BulletText("Eighth: %u", lodCounts[3]);
                
                // Transition cell stats
                ImGui::Separator();
                ImGui::Text("Transitions: %u", transitionCellCount_);
                
                // Mesh optimization info
                if (lastMeshStats_.originalVertices > 0) {
                    float reduction = 1.0f - (float)lastMeshStats_.optimizedVertices / (float)lastMeshStats_.originalVertices;
                    ImGui::Text("Optimization: %.1f%% reduction", reduction * 100.0f);
                    ImGui::Text("Cache Hit: %.1f%%", lastMeshStats_.cacheHitRatio * 100.0f);
                }
                ImGui::Separator();
                
                ImGui::Text("Camera: (%.1f, %.1f, %.1f)", 
                           cameraPos_.x, cameraPos_.y, cameraPos_.z);
                
                ImGui::Separator();
                ImGui::Text("Controls:");
                ImGui::BulletText("WASD - Move");
                ImGui::BulletText("Space/Ctrl - Up/Down");
                ImGui::BulletText("Shift - Sprint");
                ImGui::BulletText("C - Toggle Caves");
                ImGui::BulletText("G - Teleport to ground");
                
                // Cave toggle button
                auto config = terrainGenerator_->getConfig();
                if (ImGui::Checkbox("Enable Caves", &config.enableCaves)) {
                    terrainGenerator_->setConfig(config);
                }
                
                ImGui::End();
            }
            
            imguiPipeline_->endFrame();
            
            // Record ImGui draw commands
            // Note: ImGui needs to be rendered in the same render pass
            // Using inline command recording
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        }
    }

    void onInput(float deltaTime) override {
        if (!initialized_) return;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Pass events to ImGui first
            if (imguiPipeline_ && imguiPipeline_->isInitialized()) {
                ImGui_ImplSDL3_ProcessEvent(&event);
            }
            
            if (event.type == SDL_EVENT_QUIT) {
                requestClose();
                return;
            }

            // Check if ImGui wants input - if so, don't process for camera
            bool imguiWantsMouse = imguiPipeline_ && imguiPipeline_->wantsCaptureMouse();
            bool imguiWantsKeyboard = imguiPipeline_ && imguiPipeline_->wantsCaptureKeyboard();

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !imguiWantsKeyboard) {
                handleKeyDown(event.key.scancode, event.key.windowID);
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && mouseGrabbed_ && !imguiWantsMouse) {
                float sensitivity = 0.002f;
                cameraYaw_ -= event.motion.xrel * sensitivity;
                cameraPitch_ -= event.motion.yrel * sensitivity;
                cameraPitch_ = std::clamp(cameraPitch_, -1.5f, 1.5f);
                updateCameraTransform();
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !imguiWantsMouse) {
                if (event.button.button == SDL_BUTTON_LEFT && !mouseGrabbed_) {
                    SDL_SetWindowRelativeMouseMode(SDL_GetWindowFromID(event.button.windowID), true);
                    mouseGrabbed_ = true;
                }
            }
        }

        // Keyboard movement
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float speed = 25.0f * deltaTime;

        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            speed *= 5.0f;
        }

        glm::vec3 forward(std::sin(cameraYaw_), 0.0f, std::cos(cameraYaw_));
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

        if (keys[SDL_SCANCODE_W]) cameraPos_ += forward * speed;
        if (keys[SDL_SCANCODE_S]) cameraPos_ -= forward * speed;
        if (keys[SDL_SCANCODE_A]) cameraPos_ -= right * speed;
        if (keys[SDL_SCANCODE_D]) cameraPos_ += right * speed;
        if (keys[SDL_SCANCODE_SPACE]) cameraPos_.y += speed;
        if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) cameraPos_.y -= speed;

        if (cameraPos_.y < 2.0f) cameraPos_.y = 2.0f;

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

            case SDL_SCANCODE_C:
                // Toggle caves
                {
                    auto config = terrainGenerator_->getConfig();
                    config.enableCaves = !config.enableCaves;
                    terrainGenerator_->setConfig(config);
                    LOG_INFO("SmoothDemo", "Caves: %s", config.enableCaves ? "ON" : "OFF");
                }
                break;

            case SDL_SCANCODE_G:
                // Teleport to ground
                cameraPos_.y = 80.0f;
                LOG_INFO("SmoothDemo", "Teleported to height 80");
                break;

            default:
                break;
        }
    }

    void onShutdown() override {
        LOG_INFO("SmoothDemo", "Shutting down...");
        LOG_INFO("SmoothDemo", "Final: %zu chunks, %u vertices", 
                 uploadedChunks_.size(), totalVertices_);

        uploadedChunks_.clear();
        if (voxelWorld_) {
            voxelWorld_->shutdown();
        }
        smoothPipeline_.reset();
        terrainGenerator_.reset();

        LOG_INFO("SmoothDemo", "Shutdown complete");
    }

private:
    /**
     * @brief Calculate LOD level based on distance from camera
     */
    LODLevel calculateLOD(const glm::vec3& chunkCenter, const glm::vec3& cameraPos) const {
        float distance = glm::length(cameraPos - chunkCenter);
        float distanceInChunks = distance / static_cast<float>(CHUNK_SIZE);
        
        if (distanceInChunks < 4.0f) return LODLevel::Full;
        if (distanceInChunks < 8.0f) return LODLevel::Half;
        if (distanceInChunks < 16.0f) return LODLevel::Quarter;
        return LODLevel::Eighth;
    }

    /**
     * @brief Update LOD levels for all loaded chunks
     */
    void updateChunkLODs() {
        // Update LOD for each uploaded chunk
        for (auto& [coordKey, chunk] : uploadedChunks_) {
            // Find chunk coord from world offset
            ChunkCoord coord;
            coord.x = static_cast<int32_t>(std::floor(chunk.worldOffset.x / CHUNK_SIZE));
            coord.y = static_cast<int32_t>(std::floor(chunk.worldOffset.y / CHUNK_SIZE));
            coord.z = static_cast<int32_t>(std::floor(chunk.worldOffset.z / CHUNK_SIZE));
            
            glm::vec3 chunkCenter = coord.toWorldPos() + glm::vec3(CHUNK_SIZE * 0.5f);
            LODLevel newLOD = calculateLOD(chunkCenter, cameraPos_);
            
            // Store LOD in voxel world
            voxelWorld_->setChunkLOD(coord, newLOD);
        }
    }

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

    void onSmoothChunkMeshed(const ChunkCoord& coord, const SmoothMeshBuffer& buffer) {
        if (!smoothPipeline_ || buffer.empty()) return;

        // Queue for deferred upload (prevents stalls from many chunks meshing at once)
        pendingUploads_.emplace_back(coord, buffer);
    }
    
    void processPendingUploads() {
        while (uploadsThisFrame_ < MAX_UPLOADS_PER_FRAME && !pendingUploads_.empty()) {
            auto [coord, buffer] = std::move(pendingUploads_.back());
            pendingUploads_.pop_back();
            
            uploadChunkMesh(coord, buffer);
            uploadsThisFrame_++;
        }
    }
    
    void uploadChunkMesh(const ChunkCoord& coord, const SmoothMeshBuffer& buffer) {
        glm::vec3 worldOffset = coord.toWorldPos();
        uint64_t coordKey = coord.hash();

        auto it = uploadedChunks_.find(coordKey);
        uint32_t chunkIndex;

        if (it != uploadedChunks_.end()) {
            chunkIndex = it->second.chunkIndex;
            totalVertices_ -= it->second.vertexCount;  // Will re-add below
        } else {
            chunkIndex = UINT32_MAX;
            for (uint32_t i = 0; i < PipelineSmoothTerrain::MAX_CHUNKS; ++i) {
                uint32_t slot = (nextSlot_ + i) % PipelineSmoothTerrain::MAX_CHUNKS;
                if (!slotUsed_[slot]) {
                    chunkIndex = slot;
                    nextSlot_ = (slot + 1) % PipelineSmoothTerrain::MAX_CHUNKS;
                    break;
                }
            }
            if (chunkIndex == UINT32_MAX) return;
        }

        // Convert SmoothVertex to SmoothTerrainVertex for GPU
        std::vector<SmoothTerrainVertex> gpuVertices;
        gpuVertices.reserve(buffer.vertices.size());
        
        for (const auto& v : buffer.vertices) {
            SmoothTerrainVertex gpuV;
            gpuV.posX = v.position.x;
            gpuV.posY = v.position.y;
            gpuV.posZ = v.position.z;
            gpuV.normX = v.normal.x;
            gpuV.normY = v.normal.y;
            gpuV.normZ = v.normal.z;
            // Pack materialId(8) + ao(8) + texBlendU(8) + texBlendV(8)
            gpuV.packedData = v.materialId | 
                              (static_cast<uint32_t>(v.ao) << 8) |
                              (static_cast<uint32_t>(v.texBlendU) << 16) |
                              (static_cast<uint32_t>(v.texBlendV) << 24);
            gpuV.padding = 0;
            gpuVertices.push_back(gpuV);
        }

        bool uploaded = smoothPipeline_->uploadChunkMesh(
            chunkIndex,
            gpuVertices.data(),
            static_cast<uint32_t>(gpuVertices.size()),
            buffer.indices.empty() ? nullptr : buffer.indices.data(),
            static_cast<uint32_t>(buffer.indices.size()),
            worldOffset);

        if (uploaded) {
            UploadedSmoothChunk chunk;
            chunk.chunkIndex = chunkIndex;
            chunk.worldOffset = worldOffset;
            chunk.vertexCount = static_cast<uint32_t>(gpuVertices.size());
            chunk.triangleCount = static_cast<uint32_t>(buffer.triangleCount());
            uploadedChunks_[coordKey] = chunk;
            slotUsed_[chunkIndex] = true;
            totalVertices_ += chunk.vertexCount;
            
            // Update mesh optimization stats for display
            lastMeshStats_ = MeshOptimizer::getStats(buffer);
            
            // Track transition cells
            if (buffer.hasTransitions) {
                transitionCellCount_++;
            }
        }
    }

    void onChunkUnloaded(const ChunkCoord& coord, uint32_t poolIndex) {
        (void)poolIndex;
        uint64_t coordKey = coord.hash();
        auto it = uploadedChunks_.find(coordKey);
        if (it != uploadedChunks_.end()) {
            uint32_t freedSlot = it->second.chunkIndex;
            totalVertices_ -= it->second.vertexCount;
            slotUsed_[freedSlot] = false;
            if (smoothPipeline_) {
                smoothPipeline_->clearChunkMesh(freedSlot);
            }
            uploadedChunks_.erase(it);
        }
    }

    // Camera
    PerspectiveCamera* camera_ = nullptr;
    glm::vec3 cameraPos_;
    float cameraYaw_;
    float cameraPitch_;
    bool mouseGrabbed_ = false;

    // Voxel systems
    std::unique_ptr<VoxelWorldV2> voxelWorld_;
    std::unique_ptr<PipelineSmoothTerrain> smoothPipeline_;
    
    // GPU upload batching
    static constexpr uint32_t MAX_UPLOADS_PER_FRAME = 4;  // Limit GPU uploads per frame
    uint32_t uploadsThisFrame_ = 0;
    std::vector<std::pair<ChunkCoord, SmoothMeshBuffer>> pendingUploads_;
    std::unique_ptr<PerlinTerrainGenerator> terrainGenerator_;
    std::unique_ptr<PipelineImGui> imguiPipeline_;

    // Chunk tracking
    std::unordered_map<uint64_t, UploadedSmoothChunk> uploadedChunks_;
    std::array<bool, PipelineSmoothTerrain::MAX_CHUNKS> slotUsed_{};
    uint32_t nextSlot_ = 0;

    // Stats
    uint32_t totalVertices_ = 0;
    uint32_t frameCount_ = 0;
    float fpsAccumTime_ = 0.0f;
    uint32_t fpsAccumFrames_ = 0;
    float currentFps_ = 0.0f;
    FrameStats frameStats_;
    MeshOptimizer::Stats lastMeshStats_;
    uint32_t transitionCellCount_ = 0;
    bool showStatsWindow_ = true;
    bool initialized_ = false;
    
    // LOD configuration
    float lodDistances_[4] = {64.0f, 128.0f, 256.0f, 512.0f};  // LOD distance thresholds
    uint8_t maxLOD_ = 3;  // Maximum LOD level
};

int main(int argc, char* argv[]) {
    LOG_INFO("SmoothDemo", "========================================");
    LOG_INFO("SmoothDemo", "  Starting Smooth Voxel World...");
    LOG_INFO("SmoothDemo", "========================================");

    SmoothWorldDemo demo;
    return demo.run();
}

