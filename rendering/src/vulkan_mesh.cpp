#include "rendering/vulkan_mesh.h"
#include "rendering/vertex_formats.h"
#include "vulkan_backend.h"
#include <cstring>
#include <stdexcept>

namespace jupiter {
namespace rendering {

VulkanMesh::VulkanMesh() = default;

VulkanMesh::~VulkanMesh() {
    destroy();
}

VulkanMesh::VulkanMesh(VulkanMesh&& other) noexcept
    : vertexBuffer_(other.vertexBuffer_)
    , indexBuffer_(other.indexBuffer_)
    , vertexCount_(other.vertexCount_)
    , indexCount_(other.indexCount_)
    , vertexStride_(other.vertexStride_)
    , vertexFormat_(other.vertexFormat_)
    , device_(other.device_)
    , allocator_(other.allocator_) {

    std::memcpy(aabbMin_, other.aabbMin_, sizeof(aabbMin_));
    std::memcpy(aabbMax_, other.aabbMax_, sizeof(aabbMax_));

    // Null out other's resources (move semantics)
    other.vertexBuffer_ = nullptr;
    other.indexBuffer_ = nullptr;
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = VK_NULL_HANDLE;
}

VulkanMesh& VulkanMesh::operator=(VulkanMesh&& other) noexcept {
    if (this != &other) {
        destroy();

        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        vertexCount_ = other.vertexCount_;
        indexCount_ = other.indexCount_;
        vertexStride_ = other.vertexStride_;
        vertexFormat_ = other.vertexFormat_;
        device_ = other.device_;
        allocator_ = other.allocator_;

        std::memcpy(aabbMin_, other.aabbMin_, sizeof(aabbMin_));
        std::memcpy(aabbMax_, other.aabbMax_, sizeof(aabbMax_));

        other.vertexBuffer_ = nullptr;
        other.indexBuffer_ = nullptr;
        other.device_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool VulkanMesh::createFromAsset(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool commandPool, VkQueue queue,
                                 const assets::MeshAsset& meshAsset) {
    if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE) {
        return false;
    }

    // Clean up any existing resources
    destroy();

    device_ = device;
    allocator_ = allocator;

    // Copy metadata from asset
    vertexCount_ = meshAsset.vertexCount;
    indexCount_ = meshAsset.indexCount;
    vertexStride_ = meshAsset.vertexStride;
    vertexFormat_ = meshAsset.vertexFormat;
    std::memcpy(aabbMin_, meshAsset.aabbMin, sizeof(aabbMin_));
    std::memcpy(aabbMax_, meshAsset.aabbMax, sizeof(aabbMax_));

    // Create vertex buffer
    if (!meshAsset.vertexData.empty()) {
        vertexBuffer_ = new vulkan::VulkanBuffer();
        VkDeviceSize vertexBufferSize = meshAsset.vertexData.size() * sizeof(float);

        if (!uploadBufferData(commandPool, queue, meshAsset.vertexData.data(),
                             vertexBufferSize, *vertexBuffer_,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
            destroy();
            return false;
        }
    }

    // Create index buffer
    if (!meshAsset.indexData.empty()) {
        indexBuffer_ = new vulkan::VulkanBuffer();
        VkDeviceSize indexBufferSize = meshAsset.indexData.size() * sizeof(uint32_t);

        if (!uploadBufferData(commandPool, queue, meshAsset.indexData.data(),
                             indexBufferSize, *indexBuffer_,
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
            destroy();
            return false;
        }
    }

    return true;
}

void VulkanMesh::destroy() {
    if (vertexBuffer_) {
        delete vertexBuffer_;
        vertexBuffer_ = nullptr;
    }

    if (indexBuffer_) {
        delete indexBuffer_;
        indexBuffer_ = nullptr;
    }

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    vertexCount_ = 0;
    indexCount_ = 0;
}

void VulkanMesh::bind(VkCommandBuffer commandBuffer) const {
    if (!vertexBuffer_) return;

    VkBuffer vertexBuffers[] = {vertexBuffer_->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    if (indexBuffer_) {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer_->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    }
}

void VulkanMesh::draw(VkCommandBuffer commandBuffer) const {
    if (indexCount_ > 0) {
        vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
    } else if (vertexCount_ > 0) {
        vkCmdDraw(commandBuffer, vertexCount_, 1, 0, 0);
    }
}

bool VulkanMesh::uploadBufferData(VkCommandPool commandPool, VkQueue queue,
                                  const void* data, VkDeviceSize size,
                                  vulkan::VulkanBuffer& outBuffer,
                                  VkBufferUsageFlags usageFlags) {
    // Create staging buffer (CPU-accessible)
    vulkan::VulkanBuffer stagingBuffer;
    if (!stagingBuffer.create(allocator_, size,
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_CPU_ONLY)) {
        return false;
    }

    // Upload data to staging buffer
    if (!stagingBuffer.upload(data, size)) {
        return false;
    }

    // Create device-local buffer (GPU-only, fast)
    if (!outBuffer.create(allocator_, size,
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | usageFlags,
                         VMA_MEMORY_USAGE_GPU_ONLY)) {
        return false;
    }

    // Copy from staging to device-local buffer
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        return false;
    }

    // Record copy command
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer.getBuffer(), outBuffer.getBuffer(), 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // Cleanup
    vkFreeCommandBuffers(device_, commandPool, 1, &commandBuffer);

    return true;
}

bool VulkanMesh::createHostVisibleBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                          vulkan::VulkanBuffer*& outBuffer) {
    outBuffer = new vulkan::VulkanBuffer();
    
    // Create host-visible buffer (simpler, good for small procedural geometry)
    if (!outBuffer->create(allocator_, size, usage, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
        delete outBuffer;
        outBuffer = nullptr;
        return false;
    }
    
    return true;
}

bool VulkanMesh::create(VkDevice device, VmaAllocator allocator,
                        const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
                        const std::vector<uint32_t>& indices) {
    if (!vertexData || vertexCount == 0 || indices.empty()) {
        return false;
    }

    destroy();  // Clean up any existing resources

    device_ = device;
    allocator_ = allocator;
    vertexCount_ = vertexCount;
    indexCount_ = static_cast<uint32_t>(indices.size());
    vertexStride_ = vertexStride;
    vertexFormat_ = assets::VertexFormat::VERTEX_3D_LIT;

    // Create vertex buffer
    VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(vertexStride) * vertexCount;
    if (!createHostVisibleBuffer(vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer_)) {
        return false;
    }

    // Upload vertex data
    if (!vertexBuffer_->upload(vertexData, vertexBufferSize)) {
        return false;
    }

    // Create index buffer
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
    if (!createHostVisibleBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer_)) {
        return false;
    }

    // Upload index data
    if (!indexBuffer_->upload(indices.data(), indexBufferSize)) {
        return false;
    }

    return true;
}

bool VulkanMesh::create(VkDevice device, VmaAllocator allocator,
                        const std::vector<Vertex3DLit>& vertices,
                        const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        return false;
    }

    // Use the raw pointer version
    bool result = create(device, allocator, vertices.data(), 
                         static_cast<uint32_t>(vertices.size()),
                         sizeof(Vertex3DLit), indices);
    
    if (!result) {
        return false;
    }

    // Calculate AABB for lit vertices
    aabbMin_[0] = aabbMin_[1] = aabbMin_[2] = 1e30f;
    aabbMax_[0] = aabbMax_[1] = aabbMax_[2] = -1e30f;
    
    for (const auto& v : vertices) {
        if (v.pos[0] < aabbMin_[0]) aabbMin_[0] = v.pos[0];
        if (v.pos[1] < aabbMin_[1]) aabbMin_[1] = v.pos[1];
        if (v.pos[2] < aabbMin_[2]) aabbMin_[2] = v.pos[2];
        if (v.pos[0] > aabbMax_[0]) aabbMax_[0] = v.pos[0];
        if (v.pos[1] > aabbMax_[1]) aabbMax_[1] = v.pos[1];
        if (v.pos[2] > aabbMax_[2]) aabbMax_[2] = v.pos[2];
    }

    return true;
}

} // namespace rendering
} // namespace jupiter
