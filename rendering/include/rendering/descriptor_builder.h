#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace jupiter {
namespace rendering {

/**
 * @brief Fluent API for building VkDescriptorSetLayout
 *
 * Usage:
 *   DescriptorSetLayoutBuilder builder(device);
 *   builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
 *          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);
 *   VkDescriptorSetLayout layout = builder.build();
 *
 * Following CLAUDE.md: No runtime allocations in hot paths (layout creation is initialization).
 */
class DescriptorSetLayoutBuilder {
public:
    explicit DescriptorSetLayoutBuilder(VkDevice device) : device_(device) {}

    /**
     * @brief Add a binding to the descriptor set layout
     * @param binding Binding number (location in shader)
     * @param type Descriptor type (UBO, sampler, storage buffer, etc.)
     * @param stageFlags Shader stages that access this binding
     * @param count Number of descriptors (1 for single, >1 for arrays)
     * @return Reference to this builder for chaining
     */
    DescriptorSetLayoutBuilder& addBinding(
        uint32_t binding,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t count = 1)
    {
        VkDescriptorSetLayoutBinding layoutBinding = {};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = type;
        layoutBinding.descriptorCount = count;
        layoutBinding.stageFlags = stageFlags;
        layoutBinding.pImmutableSamplers = nullptr; // Can be extended later if needed

        bindings_.push_back(layoutBinding);
        bindingFlags_.push_back(0);  // No special flags for normal bindings
        return *this;
    }

    /**
     * @brief Add a binding with specific descriptor binding flags
     * @param binding Binding number
     * @param type Descriptor type
     * @param stageFlags Shader stages
     * @param count Number of descriptors
     * @param flags Descriptor binding flags (VK_DESCRIPTOR_BINDING_*_BIT)
     * @return Reference to this builder for chaining
     */
    DescriptorSetLayoutBuilder& addBindingWithFlags(
        uint32_t binding,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t count,
        VkDescriptorBindingFlags flags)
    {
        VkDescriptorSetLayoutBinding layoutBinding = {};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = type;
        layoutBinding.descriptorCount = count;
        layoutBinding.stageFlags = stageFlags;
        layoutBinding.pImmutableSamplers = nullptr;

        bindings_.push_back(layoutBinding);
        bindingFlags_.push_back(flags);

        if (flags != 0) {
            useDescriptorIndexing_ = true;
        }
        return *this;
    }

    /**
     * @brief Add an unbounded array binding for bindless resources
     *
     * Creates a binding with partially bound, variable count, and update-after-bind flags.
     * Used for bindless texture/buffer arrays.
     *
     * @param binding Binding number
     * @param type Descriptor type (typically COMBINED_IMAGE_SAMPLER or STORAGE_BUFFER)
     * @param stageFlags Shader stages
     * @param maxCount Maximum number of descriptors in the array
     * @return Reference to this builder for chaining
     */
    DescriptorSetLayoutBuilder& addUnboundedBinding(
        uint32_t binding,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t maxCount)
    {
        return addBindingWithFlags(binding, type, stageFlags, maxCount,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }

    /**
     * @brief Build the descriptor set layout
     * @return VkDescriptorSetLayout (must be destroyed by caller with vkDestroyDescriptorSetLayout)
     */
    VkDescriptorSetLayout build() {
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings_.size());
        layoutInfo.pBindings = bindings_.data();

        // Add descriptor indexing support when needed (for bindless textures)
        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
        if (useDescriptorIndexing_ && !bindingFlags_.empty()) {
            bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags_.size());
            bindingFlagsInfo.pBindingFlags = bindingFlags_.data();

            layoutInfo.pNext = &bindingFlagsInfo;
            layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        }

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        return layout;
    }

    /**
     * @brief Clear all bindings (for reuse)
     */
    void clear() {
        bindings_.clear();
        bindingFlags_.clear();
        useDescriptorIndexing_ = false;
    }

    /**
     * @brief Get number of bindings added
     */
    size_t getBindingCount() const {
        return bindings_.size();
    }

    /**
     * @brief Check if this layout uses descriptor indexing features
     */
    bool usesDescriptorIndexing() const {
        return useDescriptorIndexing_;
    }

private:
    VkDevice device_;
    std::vector<VkDescriptorSetLayoutBinding> bindings_;
    std::vector<VkDescriptorBindingFlags> bindingFlags_;
    bool useDescriptorIndexing_ = false;
};

/**
 * @brief Helper for allocating and updating descriptor sets
 *
 * Usage:
 *   DescriptorSetAllocator allocator(device, descriptorPool);
 *   VkDescriptorSet set = allocator.allocate(layout);
 *   allocator.updateBuffer(set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buffer, 0, size);
 *   allocator.updateImage(set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageView, sampler);
 *
 * Following CLAUDE.md: Batch descriptor updates to minimize API calls.
 */
class DescriptorSetAllocator {
public:
    DescriptorSetAllocator(VkDevice device, VkDescriptorPool pool)
        : device_(device), pool_(pool) {}

    /**
     * @brief Allocate a single descriptor set
     * @param layout Descriptor set layout
     * @return Allocated descriptor set (VK_NULL_HANDLE on failure)
     */
    VkDescriptorSet allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = pool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        return descriptorSet;
    }

    /**
     * @brief Allocate multiple descriptor sets
     * @param layouts Vector of descriptor set layouts
     * @param outSets Output vector of allocated descriptor sets
     * @return true if successful
     */
    bool allocateMultiple(const std::vector<VkDescriptorSetLayout>& layouts,
                         std::vector<VkDescriptorSet>& outSets) {
        outSets.resize(layouts.size());

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = pool_;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(device_, &allocInfo, outSets.data()) != VK_SUCCESS) {
            return false;
        }

        return true;
    }

    /**
     * @brief Update descriptor set with buffer binding
     * @param set Descriptor set to update
     * @param binding Binding number
     * @param type Descriptor type
     * @param buffer Buffer handle
     * @param offset Offset into buffer
     * @param range Size of buffer region (VK_WHOLE_SIZE for entire buffer)
     */
    void updateBuffer(VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                     VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = offset;
        bufferInfo.range = range;

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = set;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = type;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }

    /**
     * @brief Update descriptor set with image/sampler binding
     * @param set Descriptor set to update
     * @param binding Binding number
     * @param type Descriptor type
     * @param imageView Image view handle
     * @param sampler Sampler handle (can be VK_NULL_HANDLE for storage images)
     * @param imageLayout Image layout (typically VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
     */
    void updateImage(VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                    VkImageView imageView, VkSampler sampler,
                    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = imageLayout;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = set;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = type;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }

    /**
     * @brief Batch update multiple descriptor writes
     * @param writes Vector of descriptor writes
     *
     * More efficient than individual updates when modifying multiple bindings.
     */
    void batchUpdate(const std::vector<VkWriteDescriptorSet>& writes) {
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
                              writes.data(), 0, nullptr);
    }

private:
    VkDevice device_;
    VkDescriptorPool pool_;
};

/**
 * @brief Helper for creating descriptor pools with flexible sizing
 *
 * Usage:
 *   DescriptorPoolBuilder builder(device);
 *   builder.setMaxSets(100)
 *          .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 50)
 *          .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 50);
 *   VkDescriptorPool pool = builder.build();
 */
class DescriptorPoolBuilder {
public:
    explicit DescriptorPoolBuilder(VkDevice device) : device_(device), maxSets_(0) {}

    /**
     * @brief Set maximum number of descriptor sets that can be allocated
     */
    DescriptorPoolBuilder& setMaxSets(uint32_t maxSets) {
        maxSets_ = maxSets;
        return *this;
    }

    /**
     * @brief Add a pool size for a specific descriptor type
     * @param type Descriptor type
     * @param count Number of descriptors of this type
     */
    DescriptorPoolBuilder& addPoolSize(VkDescriptorType type, uint32_t count) {
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = type;
        poolSize.descriptorCount = count;
        poolSizes_.push_back(poolSize);
        return *this;
    }

    /**
     * @brief Build the descriptor pool
     * @param flags Optional flags (e.g., VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
     * @return VkDescriptorPool (must be destroyed by caller with vkDestroyDescriptorPool)
     */
    VkDescriptorPool build(VkDescriptorPoolCreateFlags flags = 0) {
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = flags;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes_.size());
        poolInfo.pPoolSizes = poolSizes_.data();
        poolInfo.maxSets = maxSets_;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }

        return pool;
    }

    /**
     * @brief Clear all pool sizes (for reuse)
     */
    void clear() {
        poolSizes_.clear();
        maxSets_ = 0;
    }

private:
    VkDevice device_;
    uint32_t maxSets_;
    std::vector<VkDescriptorPoolSize> poolSizes_;
};

} // namespace rendering
} // namespace jupiter
