#pragma once

#include "assets/mesh_asset.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace jupiter {
namespace rendering {

// Forward declarations
struct Vertex3DLit;

namespace vulkan {
    class VulkanBuffer;
}

/**
 * @brief GPU mesh resource (Vulkan buffers from CPU MeshAsset)
 *
 * Wraps VkBuffer for vertices and indices created from MeshAsset.
 * Separates CPU asset data from GPU resources.
 *
 * Following CLAUDE.md:
 * - No runtime allocations after creation
 * - Platform-agnostic asset input, GPU-specific output
 * - Lock-free (no shared state)
 *
 * Benefits:
 * - Assets can be loaded without GPU (server-side, tools)
 * - Same asset can create multiple GPU meshes (instancing)
 * - GPU resources cleaned up independently of assets
 *
 * Usage:
 * @code
 * AssetDatabase assetDB;
 * GLTFLoader loader(assetDB);
 * std::string modelUuid = loader.loadModel("model.gltf");
 *
 * ModelAsset model;
 * assetDB.getModelAsset(modelUuid, model);
 *
 * VulkanMesh gpuMesh;
 * gpuMesh.createFromAsset(device, allocator, commandPool, queue, model.meshes[0]);
 * @endcode
 */
class VulkanMesh {
public:
    VulkanMesh();
    ~VulkanMesh();

    // Non-copyable, movable
    VulkanMesh(const VulkanMesh&) = delete;
    VulkanMesh& operator=(const VulkanMesh&) = delete;
    VulkanMesh(VulkanMesh&& other) noexcept;
    VulkanMesh& operator=(VulkanMesh&& other) noexcept;

    /**
     * @brief Create GPU buffers from CPU MeshAsset
     *
     * Uploads vertex and index data to GPU using staging buffer.
     * Creates device-local buffers for optimal GPU performance.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param commandPool Command pool for upload
     * @param queue Graphics queue for upload
     * @param meshAsset CPU mesh asset data
     * @return true if successful
     */
    bool createFromAsset(VkDevice device, VmaAllocator allocator,
                        VkCommandPool commandPool, VkQueue queue,
                        const assets::MeshAsset& meshAsset);

    /**
     * @brief Create GPU buffers from raw vertex/index data
     *
     * Used for procedural geometry (primitives, debug shapes).
     * Uses VMA host-visible memory for simplicity (fine for debug/editor use).
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param vertexData Pointer to vertex data
     * @param vertexCount Number of vertices
     * @param vertexStride Size of each vertex in bytes
     * @param indices Index data
     * @return true if successful
     */
    bool create(VkDevice device, VmaAllocator allocator,
                const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
                const std::vector<uint32_t>& indices);

    /**
     * @brief Create GPU buffers from Vertex3DLit vector
     * 
     * Convenience overload for common vertex format.
     */
    bool create(VkDevice device, VmaAllocator allocator,
                const std::vector<struct Vertex3DLit>& vertices,
                const std::vector<uint32_t>& indices);

    /**
     * @brief Cleanup GPU resources
     */
    void destroy();

    /**
     * @brief Bind vertex and index buffers for drawing
     *
     * @param commandBuffer Command buffer to record into
     */
    void bind(VkCommandBuffer commandBuffer) const;

    /**
     * @brief Draw mesh (must be bound first)
     *
     * @param commandBuffer Command buffer to record into
     */
    void draw(VkCommandBuffer commandBuffer) const;

    // Getters
    uint32_t getVertexCount() const { return vertexCount_; }
    uint32_t getIndexCount() const { return indexCount_; }
    uint32_t getVertexStride() const { return vertexStride_; }
    assets::VertexFormat getVertexFormat() const { return vertexFormat_; }
    const float* getAABBMin() const { return aabbMin_; }
    const float* getAABBMax() const { return aabbMax_; }

    bool isValid() const { return vertexBuffer_ != nullptr; }

private:
    // GPU buffers (owned, destroyed in destructor)
    vulkan::VulkanBuffer* vertexBuffer_ = nullptr;
    vulkan::VulkanBuffer* indexBuffer_ = nullptr;

    // Mesh metadata (copied from asset)
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    uint32_t vertexStride_ = 0;
    assets::VertexFormat vertexFormat_ = assets::VertexFormat::VERTEX_2D;

    // Bounding box (copied from asset)
    float aabbMin_[3] = {0.0f, 0.0f, 0.0f};
    float aabbMax_[3] = {0.0f, 0.0f, 0.0f};

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Helper: Create staging buffer and copy to device-local buffer
    bool uploadBufferData(VkCommandPool commandPool, VkQueue queue,
                         const void* data, VkDeviceSize size,
                         vulkan::VulkanBuffer& outBuffer,
                         VkBufferUsageFlags usageFlags);

    // Helper: Create host-visible buffer (for simple procedural geometry)
    bool createHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 vulkan::VulkanBuffer*& outBuffer);
};

} // namespace rendering
} // namespace jupiter
