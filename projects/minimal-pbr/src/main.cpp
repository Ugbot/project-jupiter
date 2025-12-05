/**
 * Minimal PBR Demo
 *
 * A back-to-basics PBR implementation using reference shaders from
 * Sascha Willems' vulkan-gltf-pbr. This demo demonstrates correct PBR
 * rendering with both procedural spheres and GLTF model loading.
 *
 * Features:
 * - Procedurally generated UV sphere with material grid
 * - GLTF model loading (DamagedHelmet)
 * - PBR textures (baseColor, normal, metallicRoughness, AO, emissive)
 * - Cook-Torrance BRDF with GGX distribution
 * - Direct lighting (single directional light)
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <cstring>
#include <unordered_map>

#include "rendering/rendering.h"
#include "rendering/vertex_formats.h"
#include "rendering/texture.h"
#include "logging/logging.h"
#include "math/math.h"
#include "assets/asset_database.h"
#include "assets/gltf_loader.h"
#include "assets/model_asset.h"
#include "assets/image_asset.h"

// Include vulkan_backend.h for full type definitions
#include "../../rendering/src/vulkan_backend.h"

using namespace jupiter::rendering;
using namespace jupiter::math;
using namespace jupiter::assets;

// ============================================================================
// UBO and Push Constant Structures (must match shader layout exactly)
// ============================================================================

struct SceneUBO {
    // NOTE: Layout matches shaders - view first, then projection
    alignas(16) float view[16];
    alignas(16) float projection[16];
    alignas(16) float camPos[3];
    alignas(4)  float _pad0;
    alignas(16) float lightDir[4];   // xyz = direction (towards light), w = intensity
    alignas(16) float lightColor[4]; // xyz = color, w = unused
};

// Updated push constants to match new shader layout
struct PushConstants {
    alignas(16) float model[16];         // 64 bytes
    alignas(16) float baseColorFactor[4]; // 16 bytes
    alignas(4)  float metallicFactor;    // 4 bytes
    alignas(4)  float roughnessFactor;   // 4 bytes
    alignas(4)  uint32_t textureFlags;   // 4 bytes
    alignas(4)  float _pad;              // 4 bytes padding
    // Total: 96 bytes
};

// Texture flag bits (must match shader)
constexpr uint32_t TEX_FLAG_BASE_COLOR = 1u << 0;
constexpr uint32_t TEX_FLAG_NORMAL = 1u << 1;
constexpr uint32_t TEX_FLAG_METALLIC_ROUGHNESS = 1u << 2;
constexpr uint32_t TEX_FLAG_OCCLUSION = 1u << 3;
constexpr uint32_t TEX_FLAG_EMISSIVE = 1u << 4;

// ============================================================================
// Sphere Generation (kept for fallback/testing)
// ============================================================================

void generateSphere(float radius, uint32_t latSegments, uint32_t lonSegments,
                   std::vector<Vertex3DLit>& outVertices,
                   std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    const float PI = 3.14159265358979323846f;

    for (uint32_t lat = 0; lat <= latSegments; ++lat) {
        float theta = static_cast<float>(lat) * PI / static_cast<float>(latSegments);
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (uint32_t lon = 0; lon <= lonSegments; ++lon) {
            float phi = static_cast<float>(lon) * 2.0f * PI / static_cast<float>(lonSegments);
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            float x = cosPhi * sinTheta;
            float y = cosTheta;
            float z = sinPhi * sinTheta;

            Vertex3DLit v;
            v.pos[0] = radius * x;
            v.pos[1] = radius * y;
            v.pos[2] = radius * z;
            v.normal[0] = x;
            v.normal[1] = y;
            v.normal[2] = z;
            v.texCoord[0] = static_cast<float>(lon) / static_cast<float>(lonSegments);
            v.texCoord[1] = static_cast<float>(lat) / static_cast<float>(latSegments);
            v.tangent[0] = -sinPhi;
            v.tangent[1] = 0.0f;
            v.tangent[2] = cosPhi;
            v.tangent[3] = 1.0f;

            outVertices.push_back(v);
        }
    }

    for (uint32_t lat = 0; lat < latSegments; ++lat) {
        for (uint32_t lon = 0; lon < lonSegments; ++lon) {
            uint32_t current = lat * (lonSegments + 1) + lon;
            uint32_t next = current + lonSegments + 1;

            outIndices.push_back(current);
            outIndices.push_back(current + 1);
            outIndices.push_back(next);

            outIndices.push_back(current + 1);
            outIndices.push_back(next + 1);
            outIndices.push_back(next);
        }
    }
}

// ============================================================================
// GPU Mesh Structure (for loaded models)
// ============================================================================

struct GPUMesh {
    std::unique_ptr<vulkan::VulkanBuffer> vertexBuffer;
    std::unique_ptr<vulkan::VulkanBuffer> indexBuffer;
    uint32_t indexCount = 0;
    int32_t materialIndex = -1;
};

// ============================================================================
// GPU Material Structure (for loaded models)
// ============================================================================

struct GPUMaterial {
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    uint32_t textureFlags = 0;

    // Texture references (can be null)
    VulkanTexture* baseColorTex = nullptr;
    VulkanTexture* normalTex = nullptr;
    VulkanTexture* metallicRoughnessTex = nullptr;
    VulkanTexture* occlusionTex = nullptr;
    VulkanTexture* emissiveTex = nullptr;

    // Descriptor set for this material
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

// ============================================================================
// Minimal PBR Application
// ============================================================================

class MinimalPBRApp : public Application {
public:
    MinimalPBRApp()
        : Application("Minimal PBR Demo", 1280, 720, false)
        , cameraAngle_(0.0f)
        , sceneDescriptorSetLayout_(VK_NULL_HANDLE)
        , textureDescriptorSetLayout_(VK_NULL_HANDLE)
        , descriptorPool_(VK_NULL_HANDLE)
        , modelLoaded_(false) {
        for (auto& set : sceneDescriptorSets_) {
            set = VK_NULL_HANDLE;
        }
    }

    ~MinimalPBRApp() override = default;

protected:
    void onInit() override {
        LOG_INFO("MinimalPBR", "Initializing minimal PBR demo...");

        // Generate sphere mesh (for fallback/material testing)
        generateSphere(1.0f, 32, 64, sphereVertices_, sphereIndices_);
        LOG_INFO("MinimalPBR", "Generated sphere: %zu vertices, %zu indices",
                 sphereVertices_.size(), sphereIndices_.size());

        // Create sphere buffers
        if (!createSphereBuffers()) {
            LOG_ERROR("MinimalPBR", "Failed to create sphere buffers");
            return;
        }

        // Create default white texture for materials without textures
        if (!createDefaultTextures()) {
            LOG_ERROR("MinimalPBR", "Failed to create default textures");
            return;
        }

        // Create descriptor resources
        if (!createDescriptorResources()) {
            LOG_ERROR("MinimalPBR", "Failed to create descriptor resources");
            return;
        }

        // Create PBR pipeline
        if (!createPBRPipeline()) {
            LOG_ERROR("MinimalPBR", "Failed to create PBR pipeline");
            return;
        }

        // Try to load DamagedHelmet model
        if (!loadGLTFModel("models/DamagedHelmet/DamagedHelmet.gltf")) {
            LOG_WARN("MinimalPBR", "Failed to load DamagedHelmet, will render spheres instead");
            LOG_WARN("MinimalPBR", "Make sure models/DamagedHelmet/ exists with the GLTF files");
        }

        // Setup camera
        camera_ = createPerspectiveCamera(PI / 4.0f, 0.0f, 0.1f, 100.0f);
        setActiveCamera(camera_);

        LOG_INFO("MinimalPBR", "Initialization complete!");
        if (modelLoaded_) {
            LOG_INFO("MinimalPBR", "Rendering DamagedHelmet with PBR textures");
        } else {
            LOG_INFO("MinimalPBR", "Rendering a 5x5 grid of spheres with varying metallic/roughness");
        }
    }

    void onUpdate(float deltaTime) override {
        cameraAngle_ += deltaTime * 0.3f;

        float camDist = modelLoaded_ ? 4.0f : 12.0f;
        float camHeight = modelLoaded_ ? 1.0f : 3.0f;
        float camX = std::cos(cameraAngle_) * camDist;
        float camZ = std::sin(cameraAngle_) * camDist;

        camera_->lookAt(
            {camX, camHeight, camZ},
            {0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f}
        );

        updateSceneUBO();
    }

    void onRender() override {
        if (!pbrPipeline_) {
            LOG_WARN("MinimalPBR", "Pipeline not ready, skipping render");
            return;
        }

        vulkan::VulkanRenderer* renderer = getRenderer();
        VkCommandBuffer cmd = renderer->getCurrentCommandBuffer();
        uint32_t frameIndex = getCurrentFrameIndex();

        // Bind pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipeline_->getPipeline());

        // Bind scene descriptor set (set 0)
        VkDescriptorSet sceneSets[] = { sceneDescriptorSets_[frameIndex] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               pbrPipeline_->getLayout(), 0, 1, sceneSets, 0, nullptr);

        if (modelLoaded_) {
            renderLoadedModel(cmd);
        } else {
            renderSphereGrid(cmd);
        }
    }

    void onShutdown() override {
        LOG_INFO("MinimalPBR", "Shutting down...");

        vulkan::VulkanRenderer* renderer = getRenderer();
        if (!renderer) return;

        VkDevice device = renderer->getDevice();
        if (device == VK_NULL_HANDLE) return;

        vkDeviceWaitIdle(device);

        // Cleanup loaded model resources
        gpuMeshes_.clear();
        gpuMaterials_.clear();
        loadedTextures_.clear();

        // Cleanup default textures
        defaultWhiteTexture_.reset();
        defaultNormalTexture_.reset();

        // Cleanup uniform buffers
        for (auto& buf : uniformBuffers_) {
            buf.reset();
        }

        // Cleanup descriptor resources
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
        }
        if (sceneDescriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, sceneDescriptorSetLayout_, nullptr);
            sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
        }
        if (textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout_, nullptr);
            textureDescriptorSetLayout_ = VK_NULL_HANDLE;
        }

        pbrPipeline_.reset();
        vertexBuffer_.reset();
        indexBuffer_.reset();
    }

private:
    // Sphere mesh data
    std::vector<Vertex3DLit> sphereVertices_;
    std::vector<uint32_t> sphereIndices_;
    std::unique_ptr<vulkan::VulkanBuffer> vertexBuffer_;
    std::unique_ptr<vulkan::VulkanBuffer> indexBuffer_;

    // UBOs (one per frame)
    std::array<std::unique_ptr<vulkan::VulkanBuffer>, vulkan::MAX_FRAMES_IN_FLIGHT> uniformBuffers_;

    // Descriptors
    VkDescriptorSetLayout sceneDescriptorSetLayout_;
    VkDescriptorSetLayout textureDescriptorSetLayout_;
    VkDescriptorPool descriptorPool_;
    std::array<VkDescriptorSet, vulkan::MAX_FRAMES_IN_FLIGHT> sceneDescriptorSets_;

    // Default textures
    std::unique_ptr<VulkanTexture> defaultWhiteTexture_;
    std::unique_ptr<VulkanTexture> defaultNormalTexture_;

    // Pipeline
    std::unique_ptr<vulkan::VulkanPipeline> pbrPipeline_;

    // Camera
    PerspectiveCamera* camera_ = nullptr;
    float cameraAngle_;

    // Asset management
    std::unique_ptr<AssetDatabase> assetDatabase_;
    std::unique_ptr<GLTFLoader> gltfLoader_;

    // Loaded model data
    bool modelLoaded_;
    std::vector<GPUMesh> gpuMeshes_;
    std::vector<GPUMaterial> gpuMaterials_;
    std::unordered_map<std::string, std::unique_ptr<VulkanTexture>> loadedTextures_;

    bool createSphereBuffers() {
        vulkan::VulkanRenderer* renderer = getRenderer();
        VmaAllocator allocator = renderer->getAllocator();
        VkDevice device = renderer->getDevice();
        VkCommandPool cmdPool = renderer->getCommandPool();
        VkQueue queue = renderer->getGraphicsQueue();

        // Create vertex buffer
        vertexBuffer_ = std::make_unique<vulkan::VulkanBuffer>();
        VkDeviceSize vertexSize = sizeof(Vertex3DLit) * sphereVertices_.size();

        if (!vertexBuffer_->create(allocator, vertexSize,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY)) {
            return false;
        }

        // Upload via staging buffer
        vulkan::VulkanBuffer stagingBuffer;
        if (!stagingBuffer.create(allocator, vertexSize,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VMA_MEMORY_USAGE_CPU_ONLY)) {
            return false;
        }
        stagingBuffer.upload(sphereVertices_.data(), vertexSize);

        // Copy to GPU
        copyBuffer(device, cmdPool, queue, stagingBuffer.getBuffer(),
                  vertexBuffer_->getBuffer(), vertexSize);

        // Create index buffer
        indexBuffer_ = std::make_unique<vulkan::VulkanBuffer>();
        VkDeviceSize indexSize = sizeof(uint32_t) * sphereIndices_.size();

        if (!indexBuffer_->create(allocator, indexSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY)) {
            return false;
        }

        vulkan::VulkanBuffer indexStagingBuffer;
        if (!indexStagingBuffer.create(allocator, indexSize,
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VMA_MEMORY_USAGE_CPU_ONLY)) {
            return false;
        }
        indexStagingBuffer.upload(sphereIndices_.data(), indexSize);
        copyBuffer(device, cmdPool, queue, indexStagingBuffer.getBuffer(),
                  indexBuffer_->getBuffer(), indexSize);

        LOG_INFO("MinimalPBR", "Created sphere buffers");
        return true;
    }

    void copyBuffer(VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                   VkBuffer src, VkBuffer dst, VkDeviceSize size) {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion = {};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    }

    bool createDefaultTextures() {
        vulkan::VulkanRenderer* renderer = getRenderer();
        VkDevice device = renderer->getDevice();
        VmaAllocator allocator = renderer->getAllocator();
        VkCommandPool cmdPool = renderer->getCommandPool();
        VkQueue queue = renderer->getGraphicsQueue();

        // Create 1x1 white texture
        uint8_t whitePixel[4] = {255, 255, 255, 255};
        defaultWhiteTexture_ = std::make_unique<VulkanTexture>();
        if (!defaultWhiteTexture_->create(device, allocator, cmdPool, queue,
                                         whitePixel, 1, 1,
                                         VK_FORMAT_R8G8B8A8_SRGB, false)) {
            LOG_ERROR("MinimalPBR", "Failed to create default white texture");
            return false;
        }
        defaultWhiteTexture_->createSampler(device);

        // Create 1x1 default normal texture (flat surface: 0.5, 0.5, 1.0)
        uint8_t normalPixel[4] = {128, 128, 255, 255};
        defaultNormalTexture_ = std::make_unique<VulkanTexture>();
        if (!defaultNormalTexture_->create(device, allocator, cmdPool, queue,
                                          normalPixel, 1, 1,
                                          VK_FORMAT_R8G8B8A8_UNORM, false)) {
            LOG_ERROR("MinimalPBR", "Failed to create default normal texture");
            return false;
        }
        defaultNormalTexture_->createSampler(device);

        LOG_INFO("MinimalPBR", "Created default textures");
        return true;
    }

    bool createDescriptorResources() {
        vulkan::VulkanRenderer* renderer = getRenderer();
        VkDevice device = renderer->getDevice();
        VmaAllocator allocator = renderer->getAllocator();

        // Create uniform buffers
        for (uint32_t i = 0; i < vulkan::MAX_FRAMES_IN_FLIGHT; ++i) {
            uniformBuffers_[i] = std::make_unique<vulkan::VulkanBuffer>();
            if (!uniformBuffers_[i]->create(allocator, sizeof(SceneUBO),
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                            VMA_MEMORY_USAGE_CPU_TO_GPU)) {
                return false;
            }
        }

        // Scene descriptor set layout (set 0): uniform buffer
        VkDescriptorSetLayoutBinding uboBinding = {};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo sceneLayoutInfo = {};
        sceneLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sceneLayoutInfo.bindingCount = 1;
        sceneLayoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(device, &sceneLayoutInfo, nullptr, &sceneDescriptorSetLayout_) != VK_SUCCESS) {
            return false;
        }

        // Texture descriptor set layout (set 1): 5 combined image samplers
        std::array<VkDescriptorSetLayoutBinding, 5> textureBindings = {};
        for (uint32_t i = 0; i < 5; ++i) {
            textureBindings[i].binding = i;
            textureBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureBindings[i].descriptorCount = 1;
            textureBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo textureLayoutInfo = {};
        textureLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        textureLayoutInfo.bindingCount = static_cast<uint32_t>(textureBindings.size());
        textureLayoutInfo.pBindings = textureBindings.data();

        if (vkCreateDescriptorSetLayout(device, &textureLayoutInfo, nullptr, &textureDescriptorSetLayout_) != VK_SUCCESS) {
            return false;
        }

        // Create descriptor pool (enough for scene + materials)
        std::array<VkDescriptorPoolSize, 2> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = vulkan::MAX_FRAMES_IN_FLIGHT;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 5 * 32;  // 5 textures per material, up to 32 materials

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = vulkan::MAX_FRAMES_IN_FLIGHT + 32;  // Scene sets + material sets

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
            return false;
        }

        // Allocate scene descriptor sets
        std::array<VkDescriptorSetLayout, vulkan::MAX_FRAMES_IN_FLIGHT> sceneLayouts;
        sceneLayouts.fill(sceneDescriptorSetLayout_);

        VkDescriptorSetAllocateInfo descAllocInfo = {};
        descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descAllocInfo.descriptorPool = descriptorPool_;
        descAllocInfo.descriptorSetCount = vulkan::MAX_FRAMES_IN_FLIGHT;
        descAllocInfo.pSetLayouts = sceneLayouts.data();

        if (vkAllocateDescriptorSets(device, &descAllocInfo, sceneDescriptorSets_.data()) != VK_SUCCESS) {
            return false;
        }

        // Update scene descriptor sets
        for (uint32_t i = 0; i < vulkan::MAX_FRAMES_IN_FLIGHT; ++i) {
            VkDescriptorBufferInfo bufferInfo = {};
            bufferInfo.buffer = uniformBuffers_[i]->getBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(SceneUBO);

            VkWriteDescriptorSet write = {};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = sceneDescriptorSets_[i];
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        LOG_INFO("MinimalPBR", "Created descriptor resources");
        return true;
    }

    bool createPBRPipeline() {
        vulkan::VulkanRenderer* renderer = getRenderer();
        VkDevice device = renderer->getDevice();
        VkRenderPass renderPass = renderer->getRenderPass();
        VkExtent2D extent = renderer->getExtent();

        pbrPipeline_ = std::make_unique<vulkan::VulkanPipeline>();

        vulkan::VulkanPipeline::Config config;

        static VertexInputDescription vertex3DLitDesc = Vertex3DLit::getDescription();
        config.vertexInput = &vertex3DLitDesc;

        // Two descriptor set layouts: set 0 = scene, set 1 = textures
        config.descriptorSetLayouts.push_back(sceneDescriptorSetLayout_);
        config.descriptorSetLayouts.push_back(textureDescriptorSetLayout_);

        // Push constants
        VkPushConstantRange pushConstantRange = {};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);
        config.pushConstants.push_back(pushConstantRange);

        // No culling (like reference implementation)
        config.cullMode = VK_CULL_MODE_NONE;
        config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        config.polygonMode = VK_POLYGON_MODE_FILL;
        config.depthTestEnable = true;
        config.depthWriteEnable = true;
        config.blendEnable = false;

        if (!pbrPipeline_->create(device, renderPass, extent,
                                 "pbr.vert.spv", "pbr.frag.spv", config)) {
            return false;
        }

        LOG_INFO("MinimalPBR", "Created PBR pipeline");
        return true;
    }

    bool loadGLTFModel(const std::string& filepath) {
        LOG_INFO("MinimalPBR", "Loading GLTF model: %s", filepath.c_str());

        vulkan::VulkanRenderer* renderer = getRenderer();
        VkDevice device = renderer->getDevice();
        VmaAllocator allocator = renderer->getAllocator();
        VkCommandPool cmdPool = renderer->getCommandPool();
        VkQueue queue = renderer->getGraphicsQueue();

        // Initialize asset database
        assetDatabase_ = std::make_unique<AssetDatabase>();
        if (!assetDatabase_->initialize("assets.db")) {
            LOG_ERROR("MinimalPBR", "Failed to initialize asset database");
            return false;
        }

        // Initialize GLTF loader
        gltfLoader_ = std::make_unique<GLTFLoader>(*assetDatabase_);

        // Load model (CPU-only)
        std::string modelUuid = gltfLoader_->loadModel(filepath);
        if (modelUuid.empty()) {
            LOG_ERROR("MinimalPBR", "Failed to load model");
            return false;
        }

        // Get model asset
        ModelAsset modelAsset;
        if (!assetDatabase_->getModelAsset(modelUuid, modelAsset)) {
            LOG_ERROR("MinimalPBR", "Failed to get model asset from database");
            return false;
        }

        LOG_INFO("MinimalPBR", "Model loaded: %zu meshes, %zu materials, %zu images",
                 modelAsset.meshes.size(), modelAsset.materials.size(), modelAsset.imageUuids.size());

        // Load textures
        for (const std::string& imageUuid : modelAsset.imageUuids) {
            if (loadedTextures_.find(imageUuid) != loadedTextures_.end()) {
                continue;
            }

            ImageAsset imageAsset;
            if (!assetDatabase_->getImageAsset(imageUuid, imageAsset)) {
                LOG_WARN("MinimalPBR", "Failed to get image asset: %s", imageUuid.c_str());
                continue;
            }

            auto texture = std::make_unique<VulkanTexture>();
            if (!texture->createFromAsset(device, allocator, cmdPool, queue, imageAsset, true)) {
                LOG_WARN("MinimalPBR", "Failed to create texture: %s", imageAsset.name.c_str());
                continue;
            }
            texture->createSampler(device);

            LOG_INFO("MinimalPBR", "Loaded texture: %s (%dx%d)",
                    imageAsset.name.c_str(), imageAsset.width, imageAsset.height);
            loadedTextures_[imageUuid] = std::move(texture);
        }

        // Create GPU materials
        for (const MaterialAsset& mat : modelAsset.materials) {
            GPUMaterial gpuMat;
            std::memcpy(gpuMat.baseColorFactor, mat.baseColorFactor, sizeof(gpuMat.baseColorFactor));
            gpuMat.metallicFactor = mat.metallicFactor;
            gpuMat.roughnessFactor = mat.roughnessFactor;
            gpuMat.textureFlags = 0;

            // Link textures
            if (!mat.baseColorTextureUuid.empty()) {
                auto it = loadedTextures_.find(mat.baseColorTextureUuid);
                if (it != loadedTextures_.end()) {
                    gpuMat.baseColorTex = it->second.get();
                    gpuMat.textureFlags |= TEX_FLAG_BASE_COLOR;
                }
            }
            if (!mat.normalTextureUuid.empty()) {
                auto it = loadedTextures_.find(mat.normalTextureUuid);
                if (it != loadedTextures_.end()) {
                    gpuMat.normalTex = it->second.get();
                    gpuMat.textureFlags |= TEX_FLAG_NORMAL;
                }
            }
            if (!mat.metallicRoughnessTextureUuid.empty()) {
                auto it = loadedTextures_.find(mat.metallicRoughnessTextureUuid);
                if (it != loadedTextures_.end()) {
                    gpuMat.metallicRoughnessTex = it->second.get();
                    gpuMat.textureFlags |= TEX_FLAG_METALLIC_ROUGHNESS;
                }
            }
            if (!mat.occlusionTextureUuid.empty()) {
                auto it = loadedTextures_.find(mat.occlusionTextureUuid);
                if (it != loadedTextures_.end()) {
                    gpuMat.occlusionTex = it->second.get();
                    gpuMat.textureFlags |= TEX_FLAG_OCCLUSION;
                }
            }
            if (!mat.emissiveTextureUuid.empty()) {
                auto it = loadedTextures_.find(mat.emissiveTextureUuid);
                if (it != loadedTextures_.end()) {
                    gpuMat.emissiveTex = it->second.get();
                    gpuMat.textureFlags |= TEX_FLAG_EMISSIVE;
                }
            }

            // Allocate and update descriptor set for this material
            VkDescriptorSetAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout_;

            if (vkAllocateDescriptorSets(device, &allocInfo, &gpuMat.descriptorSet) != VK_SUCCESS) {
                LOG_ERROR("MinimalPBR", "Failed to allocate descriptor set for material");
                return false;
            }

            // Update descriptor set with textures (use defaults where not present)
            std::array<VkDescriptorImageInfo, 5> imageInfos = {};

            auto setupImageInfo = [](VkDescriptorImageInfo& info, VulkanTexture* tex, VulkanTexture* defaultTex) {
                VulkanTexture* t = tex ? tex : defaultTex;
                info.sampler = t->getSampler();
                info.imageView = t->getImageView();
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            };

            setupImageInfo(imageInfos[0], gpuMat.baseColorTex, defaultWhiteTexture_.get());
            setupImageInfo(imageInfos[1], gpuMat.normalTex, defaultNormalTexture_.get());
            setupImageInfo(imageInfos[2], gpuMat.metallicRoughnessTex, defaultWhiteTexture_.get());
            setupImageInfo(imageInfos[3], gpuMat.occlusionTex, defaultWhiteTexture_.get());
            setupImageInfo(imageInfos[4], gpuMat.emissiveTex, defaultWhiteTexture_.get());

            std::array<VkWriteDescriptorSet, 5> writes = {};
            for (uint32_t i = 0; i < 5; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = gpuMat.descriptorSet;
                writes[i].dstBinding = i;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo = &imageInfos[i];
            }

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            gpuMaterials_.push_back(gpuMat);
            LOG_INFO("MinimalPBR", "Created GPU material: flags=0x%x", gpuMat.textureFlags);
        }

        // Create GPU meshes
        for (const MeshAsset& mesh : modelAsset.meshes) {
            GPUMesh gpuMesh;
            gpuMesh.materialIndex = -1;

            // Find material index
            for (size_t i = 0; i < modelAsset.materials.size(); ++i) {
                if (modelAsset.materials[i].uuid == mesh.materialUuid) {
                    gpuMesh.materialIndex = static_cast<int32_t>(i);
                    break;
                }
            }

            // Create vertex buffer
            gpuMesh.vertexBuffer = std::make_unique<vulkan::VulkanBuffer>();
            VkDeviceSize vertexSize = mesh.vertexData.size() * sizeof(float);

            if (!gpuMesh.vertexBuffer->create(allocator, vertexSize,
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              VMA_MEMORY_USAGE_GPU_ONLY)) {
                LOG_ERROR("MinimalPBR", "Failed to create mesh vertex buffer");
                return false;
            }

            vulkan::VulkanBuffer vertexStaging;
            vertexStaging.create(allocator, vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            vertexStaging.upload(mesh.vertexData.data(), vertexSize);
            copyBuffer(device, cmdPool, queue, vertexStaging.getBuffer(),
                      gpuMesh.vertexBuffer->getBuffer(), vertexSize);

            // Create index buffer
            gpuMesh.indexBuffer = std::make_unique<vulkan::VulkanBuffer>();
            VkDeviceSize indexSize = mesh.indexData.size() * sizeof(uint32_t);
            gpuMesh.indexCount = static_cast<uint32_t>(mesh.indexData.size());

            if (!gpuMesh.indexBuffer->create(allocator, indexSize,
                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VMA_MEMORY_USAGE_GPU_ONLY)) {
                LOG_ERROR("MinimalPBR", "Failed to create mesh index buffer");
                return false;
            }

            vulkan::VulkanBuffer indexStaging;
            indexStaging.create(allocator, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            indexStaging.upload(mesh.indexData.data(), indexSize);
            copyBuffer(device, cmdPool, queue, indexStaging.getBuffer(),
                      gpuMesh.indexBuffer->getBuffer(), indexSize);

            gpuMeshes_.push_back(std::move(gpuMesh));
            LOG_INFO("MinimalPBR", "Created GPU mesh: %u indices, material %d",
                    gpuMesh.indexCount, gpuMesh.materialIndex);
        }

        modelLoaded_ = true;
        LOG_INFO("MinimalPBR", "GLTF model loaded successfully");
        return true;
    }

    void renderLoadedModel(VkCommandBuffer cmd) {
        for (const GPUMesh& mesh : gpuMeshes_) {
            // Get material
            const GPUMaterial* mat = nullptr;
            if (mesh.materialIndex >= 0 && mesh.materialIndex < static_cast<int32_t>(gpuMaterials_.size())) {
                mat = &gpuMaterials_[mesh.materialIndex];
            }

            // Bind texture descriptor set (set 1)
            if (mat && mat->descriptorSet != VK_NULL_HANDLE) {
                VkDescriptorSet texSets[] = { mat->descriptorSet };
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       pbrPipeline_->getLayout(), 1, 1, texSets, 0, nullptr);
            }

            // Bind vertex and index buffers
            VkBuffer vertexBuffers[] = { mesh.vertexBuffer->getBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, mesh.indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Setup push constants
            PushConstants push = {};
            Matrix4x4 model = Matrix4x4::identity();
            std::memcpy(push.model, model.data(), sizeof(push.model));

            if (mat) {
                std::memcpy(push.baseColorFactor, mat->baseColorFactor, sizeof(push.baseColorFactor));
                push.metallicFactor = mat->metallicFactor;
                push.roughnessFactor = mat->roughnessFactor;
                push.textureFlags = mat->textureFlags;
            } else {
                push.baseColorFactor[0] = 0.8f;
                push.baseColorFactor[1] = 0.8f;
                push.baseColorFactor[2] = 0.8f;
                push.baseColorFactor[3] = 1.0f;
                push.metallicFactor = 0.0f;
                push.roughnessFactor = 0.5f;
                push.textureFlags = 0;
            }

            vkCmdPushConstants(cmd, pbrPipeline_->getLayout(),
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(PushConstants), &push);

            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        }
    }

    void renderSphereGrid(VkCommandBuffer cmd) {
        // Bind default texture descriptor set for spheres
        // We need a descriptor set even for spheres (shader expects it)
        // Create a simple one using default textures

        // Bind vertex and index buffers
        VkBuffer vertexBuffers[] = { vertexBuffer_->getBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer_->getBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Create and bind default texture descriptor set if not already done
        static VkDescriptorSet defaultTexSet = VK_NULL_HANDLE;
        if (defaultTexSet == VK_NULL_HANDLE) {
            vulkan::VulkanRenderer* renderer = getRenderer();
            VkDevice device = renderer->getDevice();

            VkDescriptorSetAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout_;

            vkAllocateDescriptorSets(device, &allocInfo, &defaultTexSet);

            std::array<VkDescriptorImageInfo, 5> imageInfos = {};
            for (auto& info : imageInfos) {
                info.sampler = defaultWhiteTexture_->getSampler();
                info.imageView = defaultWhiteTexture_->getImageView();
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            // Normal texture uses default normal
            imageInfos[1].sampler = defaultNormalTexture_->getSampler();
            imageInfos[1].imageView = defaultNormalTexture_->getImageView();

            std::array<VkWriteDescriptorSet, 5> writes = {};
            for (uint32_t i = 0; i < 5; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = defaultTexSet;
                writes[i].dstBinding = i;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo = &imageInfos[i];
            }

            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               pbrPipeline_->getLayout(), 1, 1, &defaultTexSet, 0, nullptr);

        // Render 5x5 grid
        const int gridSize = 5;
        const float spacing = 2.5f;
        const float gridOffset = (gridSize - 1) * spacing * 0.5f;

        for (int row = 0; row < gridSize; ++row) {
            for (int col = 0; col < gridSize; ++col) {
                float x = col * spacing - gridOffset;
                float y = row * spacing - gridOffset;

                float metallic = static_cast<float>(col) / (gridSize - 1);
                float roughness = 0.1f + static_cast<float>(row) / (gridSize - 1) * 0.8f;

                PushConstants push = {};
                Matrix4x4 model = Matrix4x4::translation(Vector3{x, y, 0.0f});
                std::memcpy(push.model, model.data(), sizeof(push.model));

                push.baseColorFactor[0] = 0.8f;
                push.baseColorFactor[1] = 0.8f;
                push.baseColorFactor[2] = 0.8f;
                push.baseColorFactor[3] = 1.0f;
                push.metallicFactor = metallic;
                push.roughnessFactor = roughness;
                push.textureFlags = 0;  // No textures for spheres

                vkCmdPushConstants(cmd, pbrPipeline_->getLayout(),
                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                  0, sizeof(PushConstants), &push);

                vkCmdDrawIndexed(cmd, static_cast<uint32_t>(sphereIndices_.size()), 1, 0, 0, 0);
            }
        }
    }

    void updateSceneUBO() {
        if (!camera_) return;

        uint32_t frameIndex = getCurrentFrameIndex();

        SceneUBO ubo = {};

        Matrix4x4 view = camera_->getViewMatrix();
        Matrix4x4 proj = camera_->getProjectionMatrix();

        std::memcpy(ubo.projection, proj.data(), sizeof(ubo.projection));
        std::memcpy(ubo.view, view.data(), sizeof(ubo.view));

        float camDist = modelLoaded_ ? 4.0f : 12.0f;
        float camX = std::cos(cameraAngle_) * camDist;
        float camZ = std::sin(cameraAngle_) * camDist;
        ubo.camPos[0] = camX;
        ubo.camPos[1] = modelLoaded_ ? 1.0f : 3.0f;
        ubo.camPos[2] = camZ;

        // Light direction (from upper-right-front)
        ubo.lightDir[0] = 0.5f;
        ubo.lightDir[1] = 1.0f;
        ubo.lightDir[2] = 0.5f;
        ubo.lightDir[3] = 3.0f;  // intensity

        // Light color
        ubo.lightColor[0] = 1.0f;
        ubo.lightColor[1] = 0.95f;
        ubo.lightColor[2] = 0.9f;
        ubo.lightColor[3] = 1.0f;

        uniformBuffers_[frameIndex]->upload(&ubo, sizeof(SceneUBO));
    }
};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    jupiter::logging::initialize("minimal_pbr.log");

    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "  Minimal PBR Demo");
    LOG_INFO("Main", "  Project Jupiter Engine");
    LOG_INFO("Main", "========================================");

    MinimalPBRApp app;
    int result = app.run();

    jupiter::logging::shutdown();
    return result;
}
