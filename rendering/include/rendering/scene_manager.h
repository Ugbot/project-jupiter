#pragma once

#include "vulkan_mesh.h"
#include "material.h"
#include "material_system.h"
#include "assets/model_asset.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace jupiter {
namespace rendering {

/**
 * @brief Single drawable instance (mesh + material + transform)
 *
 * Represents one renderable entity in the scene.
 * Multiple renderables can share the same mesh/material (instancing).
 *
 * Following CLAUDE.md:
 * - POD layout where possible
 * - Cache-friendly (transforms stored inline)
 * - No runtime allocations
 */
struct Renderable {
    VulkanMesh* mesh = nullptr;      // GPU mesh (not owned)
    Material* material = nullptr;    // GPU material (not owned)
    glm::mat4 transform = glm::mat4(1.0f);  // World transform
    bool visible = true;
    uint32_t layerMask = 1;         // Render layer (for filtering)

    // Voxel-specific fields (valid when isVoxel() returns true)
    glm::vec3 chunkOffset{0.0f};    // World position of chunk origin
    glm::vec3 voxelScale{1.0f};     // stb_voxel_render transform scale
    uint8_t voxelLod = 0;           // LOD level (0=Vertex3DLit, 2+=VoxelVertexGPU)
    bool isVoxelChunk = false;      // True if this is a voxel chunk

    Renderable() = default;

    Renderable(VulkanMesh* m, Material* mat, const glm::mat4& t = glm::mat4(1.0f))
        : mesh(m), material(mat), transform(t) {}

    bool isValid() const {
        return mesh && mesh->isValid() && material && material->isValid();
    }

    /**
     * @brief Check if this is a voxel chunk renderable
     *
     * Voxel chunks with LOD >= 2 use compact VoxelVertexGPU format,
     * while LOD 0-1 use standard Vertex3DLit format.
     */
    bool isVoxel() const { return isVoxelChunk; }

    /**
     * @brief Check if this voxel uses compact format (for LOD 2+)
     *
     * Returns true if this voxel should use the voxel shader pipeline
     * instead of the standard PBR pipeline.
     */
    bool usesCompactVoxelFormat() const {
        return isVoxelChunk && voxelLod >= 2;
    }
};

/**
 * @brief Handle to a renderable in the scene
 *
 * Type-safe index into SceneManager's renderable array.
 * Supports hot-reload via version tracking.
 */
struct RenderableHandle {
    uint32_t index = UINT32_MAX;
    uint32_t version = 0;

    RenderableHandle() = default;
    RenderableHandle(uint32_t idx, uint32_t ver) : index(idx), version(ver) {}

    bool isValid() const { return index != UINT32_MAX; }
};

/**
 * @brief Collection of renderables from a ModelAsset (scene graph hierarchy)
 *
 * Creates GPU resources from CPU ModelAsset and manages scene graph.
 * Returns handles to individual renderables for the SceneManager.
 *
 * Following CLAUDE.md:
 * - Pre-allocates all resources during creation
 * - Data-oriented, cache-friendly layout
 * - No runtime allocations after creation
 *
 * Benefits:
 * - Same ModelAsset creates multiple RenderableModel instances
 * - Independent transforms per instance
 * - Automatic scene graph traversal
 *
 * Usage:
 * @code
 * ModelAsset modelAsset;
 * assetDB.getModelAsset(uuid, modelAsset);
 *
 * RenderableModel* model = sceneManager.createRenderableModel(modelAsset, textures);
 * std::vector<RenderableHandle> handles = model->getRenderableHandles();
 * @endcode
 */
class RenderableModel {
public:
    RenderableModel() = default;
    ~RenderableModel();

    // Non-copyable, movable
    RenderableModel(const RenderableModel&) = delete;
    RenderableModel& operator=(const RenderableModel&) = delete;
    RenderableModel(RenderableModel&& other) noexcept;
    RenderableModel& operator=(RenderableModel&& other) noexcept;

    /**
     * @brief Create GPU resources from ModelAsset
     *
     * Creates VulkanMesh for each mesh, Materials for each material,
     * and builds scene graph hierarchy.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for uploads
     * @param queue Graphics queue for uploads
     * @param materialSystem Material system for creating materials
     * @param modelAsset CPU model asset
     * @param textures Map of image UUID -> VulkanTexture
     * @return true if successful
     */
    bool createFromAsset(VkDevice device, VmaAllocator allocator,
                        VkCommandPool commandPool, VkQueue queue,
                        MaterialSystem& materialSystem,
                        const assets::ModelAsset& modelAsset,
                        const std::unordered_map<std::string, VulkanTexture*>& textures);

    /**
     * @brief Cleanup all GPU resources
     */
    void destroy();

    /**
     * @brief Get renderables for scene manager
     *
     * Returns vector of Renderable instances with world transforms.
     * Updates transforms from scene graph hierarchy.
     *
     * @return Vector of renderables (ready to add to SceneManager)
     */
    std::vector<Renderable> getRenderables();

    /**
     * @brief Update scene graph transforms
     *
     * Recalculates world transforms for all nodes.
     * Call after modifying node local transforms.
     */
    void updateTransforms();

    /**
     * @brief Set root transform (applied to all nodes)
     */
    void setRootTransform(const glm::mat4& transform) {
        rootTransform_ = transform;
        updateTransforms();
    }

    const glm::mat4& getRootTransform() const { return rootTransform_; }

    bool isValid() const { return !meshes_.empty(); }

private:
    // GPU resources (owned)
    std::vector<VulkanMesh*> meshes_;
    std::vector<Material*> materials_;

    // Material UUID for each mesh (parallel to meshes_)
    std::vector<std::string> meshMaterialUuids_;

    // UUID -> Material* mapping for fast lookup
    std::unordered_map<std::string, Material*> materialsByUuid_;

    // Scene graph (flattened, matches ModelAsset structure)
    struct Node {
        glm::mat4 localTransform;
        glm::mat4 worldTransform;
        int32_t parentIndex;
        int32_t meshIndex;
        bool visible;
    };

    std::vector<Node> nodes_;
    std::vector<int32_t> rootNodeIndices_;

    glm::mat4 rootTransform_ = glm::mat4(1.0f);

    VkDevice device_ = VK_NULL_HANDLE;

    // Helper: Recursively update transforms
    void updateNodeTransform(uint32_t nodeIndex, const glm::mat4& parentTransform);
};

/**
 * @brief Scene manager for batching and rendering all renderables
 *
 * Manages all renderables in the scene.
 * Provides material-batched rendering for optimal performance.
 *
 * Following CLAUDE.md:
 * - Pre-allocated renderable storage
 * - No runtime allocations during rendering
 * - Data-oriented, cache-friendly batching
 * - Lock-free (single-threaded rendering)
 *
 * Rendering Strategy:
 * - Sort renderables by material (minimizes descriptor set changes)
 * - Batch draw calls by material
 * - Supports render layers for selective rendering
 *
 * Benefits:
 * - Automatic batching for performance
 * - Easy add/remove renderables
 * - Hot-reload support via handles
 *
 * Usage:
 * @code
 * SceneManager sceneManager;
 * sceneManager.initialize(1000);  // Max 1000 renderables
 *
 * RenderableHandle handle = sceneManager.addRenderable(renderable);
 *
 * // Rendering
 * sceneManager.render(commandBuffer, pipelineLayout, pipeline, globalDescriptorSet);
 * @endcode
 */
class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    // Non-copyable
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    /**
     * @brief Initialize scene manager
     *
     * Pre-allocates storage for renderables.
     *
     * @param maxRenderables Maximum number of renderables
     * @return true if successful
     */
    bool initialize(uint32_t maxRenderables = 1000);

    /**
     * @brief Cleanup scene manager
     */
    void destroy();

    /**
     * @brief Add renderable to scene
     *
     * @param renderable Renderable to add
     * @return Handle to renderable (for updates/removal)
     */
    RenderableHandle addRenderable(const Renderable& renderable);

    /**
     * @brief Remove renderable from scene
     *
     * @param handle Handle to renderable
     */
    void removeRenderable(RenderableHandle handle);

    /**
     * @brief Update renderable transform
     *
     * @param handle Handle to renderable
     * @param transform New world transform
     */
    void updateTransform(RenderableHandle handle, const glm::mat4& transform);

    /**
     * @brief Create RenderableModel from ModelAsset
     *
     * Helper that creates all GPU resources and returns RenderableModel.
     * RenderableModel is owned by SceneManager.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for uploads
     * @param queue Graphics queue for uploads
     * @param materialSystem Material system
     * @param modelAsset CPU model asset
     * @param textures Map of image UUID -> VulkanTexture
     * @return Pointer to created RenderableModel (owned by SceneManager)
     */
    RenderableModel* createRenderableModel(VkDevice device, VmaAllocator allocator,
                                          VkCommandPool commandPool, VkQueue queue,
                                          MaterialSystem& materialSystem,
                                          const assets::ModelAsset& modelAsset,
                                          const std::unordered_map<std::string, VulkanTexture*>& textures);

    /**
     * @brief Render all visible renderables
     *
     * Batches renderables by material and renders.
     * Assumes pipeline is already bound.
     *
     * @param commandBuffer Command buffer
     * @param pipelineLayout Pipeline layout
     * @param pipeline Graphics pipeline (must support Set 0 and Set 1)
     * @param globalDescriptorSet Global descriptor set (Set 0: camera/lights)
     * @param layerMask Render only renderables matching this layer mask (default: all)
     */
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
               VkPipeline pipeline, VkDescriptorSet globalDescriptorSet,
               uint32_t layerMask = 0xFFFFFFFF);

    /**
     * @brief Clear all renderables
     */
    void clear();

    // Getters
    uint32_t getRenderableCount() const { return static_cast<uint32_t>(renderables_.size()); }
    uint32_t getModelCount() const { return static_cast<uint32_t>(models_.size()); }
    
    /**
     * @brief Get all renderables (for iteration in pipelines)
     */
    const std::vector<Renderable>& getRenderables() const { return renderables_; }

    bool isInitialized() const { return maxRenderables_ > 0; }

private:
    std::vector<Renderable> renderables_;
    std::vector<uint32_t> renderableVersions_;  // For handle versioning
    std::vector<uint32_t> freeIndices_;         // Free list for removed renderables

    std::vector<RenderableModel*> models_;  // Owned models

    uint32_t maxRenderables_ = 0;

    // Rendering: Material batching
    struct RenderBatch {
        Material* material;
        std::vector<uint32_t> renderableIndices;
    };

    std::vector<RenderBatch> batches_;  // Temporary (cleared each frame)

    // Helper: Batch renderables by material
    void buildRenderBatches(uint32_t layerMask);
};

} // namespace rendering
} // namespace jupiter
