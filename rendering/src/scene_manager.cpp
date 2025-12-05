#include "rendering/scene_manager.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

namespace jupiter {
namespace rendering {

// ============================================================================
// RenderableModel Implementation
// ============================================================================

RenderableModel::~RenderableModel() {
    destroy();
}

RenderableModel::RenderableModel(RenderableModel&& other) noexcept
    : meshes_(std::move(other.meshes_))
    , materials_(std::move(other.materials_))
    , nodes_(std::move(other.nodes_))
    , rootNodeIndices_(std::move(other.rootNodeIndices_))
    , rootTransform_(other.rootTransform_)
    , device_(other.device_) {

    other.device_ = VK_NULL_HANDLE;
}

RenderableModel& RenderableModel::operator=(RenderableModel&& other) noexcept {
    if (this != &other) {
        destroy();

        meshes_ = std::move(other.meshes_);
        materials_ = std::move(other.materials_);
        nodes_ = std::move(other.nodes_);
        rootNodeIndices_ = std::move(other.rootNodeIndices_);
        rootTransform_ = other.rootTransform_;
        device_ = other.device_;

        other.device_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool RenderableModel::createFromAsset(VkDevice device, VmaAllocator allocator,
                                     VkCommandPool commandPool, VkQueue queue,
                                     MaterialSystem& materialSystem,
                                     const assets::ModelAsset& modelAsset,
                                     const std::unordered_map<std::string, VulkanTexture*>& textures) {
    if (device == VK_NULL_HANDLE) {
        return false;
    }

    destroy();

    device_ = device;

    // Create GPU meshes from CPU mesh assets
    meshes_.reserve(modelAsset.meshes.size());
    meshMaterialUuids_.reserve(modelAsset.meshes.size());
    for (const auto& meshAsset : modelAsset.meshes) {
        VulkanMesh* mesh = new VulkanMesh();
        if (!mesh->createFromAsset(device, allocator, commandPool, queue, meshAsset)) {
            delete mesh;
            destroy();
            return false;
        }
        meshes_.push_back(mesh);
        meshMaterialUuids_.push_back(meshAsset.materialUuid);  // Store material UUID for this mesh
    }

    // Create GPU materials from CPU material assets
    materials_.reserve(modelAsset.materials.size());
    for (const auto& materialAsset : modelAsset.materials) {
        Material* material = materialSystem.createMaterial(materialAsset, textures);
        if (!material) {
            destroy();
            return false;
        }
        materials_.push_back(material);
        materialsByUuid_[materialAsset.uuid] = material;  // Build UUID -> Material* mapping
    }

    // Build scene graph from model nodes
    nodes_.reserve(modelAsset.nodes.size());
    for (const auto& modelNode : modelAsset.nodes) {
        Node node;

        // Convert float[16] to glm::mat4 (column-major)
        node.localTransform = glm::make_mat4(modelNode.localTransform);
        node.worldTransform = glm::make_mat4(modelNode.worldTransform);
        node.parentIndex = modelNode.parentIndex;
        node.meshIndex = modelNode.meshIndex;
        node.visible = modelNode.visible;

        nodes_.push_back(node);
    }

    rootNodeIndices_ = modelAsset.rootNodeIndices;

    // Update transforms
    updateTransforms();

    return true;
}

void RenderableModel::destroy() {
    // Destroy meshes (owned)
    for (VulkanMesh* mesh : meshes_) {
        if (mesh) {
            mesh->destroy();
            delete mesh;
        }
    }
    meshes_.clear();
    meshMaterialUuids_.clear();

    // Materials are owned by MaterialSystem, don't delete
    materials_.clear();
    materialsByUuid_.clear();

    nodes_.clear();
    rootNodeIndices_.clear();

    device_ = VK_NULL_HANDLE;
}

std::vector<Renderable> RenderableModel::getRenderables() {
    std::vector<Renderable> renderables;
    renderables.reserve(nodes_.size());

    for (size_t i = 0; i < nodes_.size(); i++) {
        const Node& node = nodes_[i];

        if (!node.visible || node.meshIndex < 0) {
            continue;  // Skip invisible or non-mesh nodes
        }

        if (node.meshIndex >= static_cast<int32_t>(meshes_.size())) {
            continue;  // Invalid mesh index
        }

        VulkanMesh* mesh = meshes_[node.meshIndex];

        // Get material using UUID from mesh asset
        Material* material = nullptr;
        const std::string& materialUuid = meshMaterialUuids_[node.meshIndex];
        auto it = materialsByUuid_.find(materialUuid);
        if (it != materialsByUuid_.end()) {
            material = it->second;
        }

        if (!material) {
            continue;  // No material for this mesh
        }

        Renderable renderable;
        renderable.mesh = mesh;
        renderable.material = material;
        renderable.transform = node.worldTransform;
        renderable.visible = node.visible;

        renderables.push_back(renderable);
    }

    return renderables;
}

void RenderableModel::updateTransforms() {
    // Update root nodes
    for (int32_t rootIdx : rootNodeIndices_) {
        if (rootIdx >= 0 && rootIdx < static_cast<int32_t>(nodes_.size())) {
            updateNodeTransform(rootIdx, rootTransform_);
        }
    }
}

void RenderableModel::updateNodeTransform(uint32_t nodeIndex, const glm::mat4& parentTransform) {
    if (nodeIndex >= nodes_.size()) {
        return;
    }

    Node& node = nodes_[nodeIndex];
    node.worldTransform = parentTransform * node.localTransform;

    // Recursively update children (find children by scanning all nodes)
    for (size_t i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].parentIndex == static_cast<int32_t>(nodeIndex)) {
            updateNodeTransform(i, node.worldTransform);
        }
    }
}

// ============================================================================
// SceneManager Implementation
// ============================================================================

SceneManager::SceneManager() = default;

SceneManager::~SceneManager() {
    destroy();
}

bool SceneManager::initialize(uint32_t maxRenderables) {
    if (maxRenderables == 0) {
        return false;
    }

    destroy();

    maxRenderables_ = maxRenderables;

    // Pre-allocate storage (avoid runtime allocations)
    renderables_.reserve(maxRenderables);
    renderableVersions_.reserve(maxRenderables);
    freeIndices_.reserve(maxRenderables / 2);  // Estimate

    return true;
}

void SceneManager::destroy() {
    // Destroy all models (owned)
    for (RenderableModel* model : models_) {
        if (model) {
            model->destroy();
            delete model;
        }
    }
    models_.clear();

    renderables_.clear();
    renderableVersions_.clear();
    freeIndices_.clear();
    batches_.clear();

    maxRenderables_ = 0;
}

RenderableHandle SceneManager::addRenderable(const Renderable& renderable) {
    uint32_t index;

    if (!freeIndices_.empty()) {
        // Reuse free slot
        index = freeIndices_.back();
        freeIndices_.pop_back();
        renderables_[index] = renderable;
        renderableVersions_[index]++;
    } else {
        // Add new slot
        if (renderables_.size() >= maxRenderables_) {
            return RenderableHandle();  // Pool exhausted
        }

        index = static_cast<uint32_t>(renderables_.size());
        renderables_.push_back(renderable);
        renderableVersions_.push_back(0);
    }

    return RenderableHandle(index, renderableVersions_[index]);
}

void SceneManager::removeRenderable(RenderableHandle handle) {
    if (!handle.isValid() || handle.index >= renderables_.size()) {
        return;
    }

    if (renderableVersions_[handle.index] != handle.version) {
        return;  // Stale handle
    }

    // Invalidate renderable
    renderables_[handle.index].mesh = nullptr;
    renderables_[handle.index].material = nullptr;

    // Add to free list
    freeIndices_.push_back(handle.index);
}

void SceneManager::updateTransform(RenderableHandle handle, const glm::mat4& transform) {
    if (!handle.isValid() || handle.index >= renderables_.size()) {
        return;
    }

    if (renderableVersions_[handle.index] != handle.version) {
        return;  // Stale handle
    }

    renderables_[handle.index].transform = transform;
}

RenderableModel* SceneManager::createRenderableModel(VkDevice device, VmaAllocator allocator,
                                                     VkCommandPool commandPool, VkQueue queue,
                                                     MaterialSystem& materialSystem,
                                                     const assets::ModelAsset& modelAsset,
                                                     const std::unordered_map<std::string, VulkanTexture*>& textures) {
    RenderableModel* model = new RenderableModel();

    if (!model->createFromAsset(device, allocator, commandPool, queue,
                               materialSystem, modelAsset, textures)) {
        delete model;
        return nullptr;
    }

    models_.push_back(model);
    return model;
}

void SceneManager::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                         VkPipeline pipeline, VkDescriptorSet globalDescriptorSet,
                         uint32_t layerMask) {
    // Build material batches
    buildRenderBatches(layerMask);

    // Bind global descriptor set (Set 0: camera/lights)
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, 0, 1, &globalDescriptorSet, 0, nullptr);

    // Render each batch (sorted by material)
    for (const RenderBatch& batch : batches_) {
        if (batch.renderableIndices.empty()) {
            continue;
        }

        // Bind material descriptor set (Set 1: textures)
        batch.material->bind(commandBuffer, pipelineLayout, 1);

        // Draw all renderables with this material
        for (uint32_t index : batch.renderableIndices) {
            const Renderable& renderable = renderables_[index];

            if (!renderable.isValid() || !renderable.visible) {
                continue;
            }

            // Push model transform matrix as push constant
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                             VK_SHADER_STAGE_VERTEX_BIT, 0, 64,
                             &renderable.transform);

            // Bind mesh and draw
            renderable.mesh->bind(commandBuffer);
            renderable.mesh->draw(commandBuffer);
        }
    }

    // Clear batches for next frame
    batches_.clear();
}

void SceneManager::clear() {
    renderables_.clear();
    renderableVersions_.clear();
    freeIndices_.clear();
}

void SceneManager::buildRenderBatches(uint32_t layerMask) {
    batches_.clear();

    // Group renderables by material
    std::unordered_map<Material*, std::vector<uint32_t>> materialGroups;

    for (size_t i = 0; i < renderables_.size(); i++) {
        const Renderable& renderable = renderables_[i];

        if (!renderable.isValid() || !renderable.visible) {
            continue;
        }

        if ((renderable.layerMask & layerMask) == 0) {
            continue;  // Layer filtered out
        }

        materialGroups[renderable.material].push_back(static_cast<uint32_t>(i));
    }

    // Convert to batches
    batches_.reserve(materialGroups.size());
    for (auto& [material, indices] : materialGroups) {
        RenderBatch batch;
        batch.material = material;
        batch.renderableIndices = std::move(indices);
        batches_.push_back(std::move(batch));
    }
}

} // namespace rendering
} // namespace jupiter
