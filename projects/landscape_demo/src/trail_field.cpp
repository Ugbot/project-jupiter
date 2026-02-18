#include "trail_field.h"
#include "logging/logging.h"
#include "../../../rendering/src/vulkan_backend.h"  // Internal header for VulkanBuffer
#include <algorithm>
#include <cstring>

using namespace jupiter;
using namespace jupiter::rendering;
using namespace jupiter::rendering::vulkan;

namespace landscape {

TrailField::~TrailField() {
    destroy();
    delete eventsSSBO_;
}

void TrailField::destroy() {
    if (device_ != VK_NULL_HANDLE) {
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
            descriptorPool_ = VK_NULL_HANDLE;
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
            descriptorSetLayout_ = VK_NULL_HANDLE;
        }
    }
    
    updatePipeline_.destroy();
    
    if (eventsSSBO_) {
        eventsSSBO_->destroy();
    }
    
    intensityA_.destroy();
    intensityB_.destroy();
    dirA_.destroy();
    dirB_.destroy();
    
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

bool TrailField::initialize(VkDevice device, VmaAllocator allocator,
                             VkCommandPool commandPool, VkQueue graphicsQueue,
                             float worldSize, uint32_t resolution) {
    device_ = device;
    allocator_ = allocator;
    worldSize_ = worldSize;
    resolution_ = resolution;
    
    LOG_INFO("TrailField", "Initializing trail field (%.0fm, %ux%u)", 
             worldSize, resolution, resolution);
    
    // Preallocate CPU staging vector
    eventsStaging_.reserve(MAX_TRAIL_EVENTS);
    
    // Create textures
    if (!createTextures(commandPool, graphicsQueue)) {
        LOG_ERROR("TrailField", "Failed to create textures");
        return false;
    }
    
    // Create events SSBO
    eventsSSBO_ = new VulkanBuffer();
    VkDeviceSize eventsSize = sizeof(TrailEvent) * MAX_TRAIL_EVENTS;
    if (!eventsSSBO_->create(allocator, eventsSize,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VMA_MEMORY_USAGE_CPU_TO_GPU)) {
        LOG_ERROR("TrailField", "Failed to create events SSBO");
        return false;
    }
    
    // Create descriptors
    if (!createDescriptors()) {
        LOG_ERROR("TrailField", "Failed to create descriptors");
        return false;
    }
    
    // Create compute pipeline
    if (!createPipeline()) {
        LOG_ERROR("TrailField", "Failed to create pipeline");
        return false;
    }
    
    // Set initial pointers
    currentIntensity_ = &intensityA_;
    currentDir_ = &dirA_;
    prevIntensity_ = &intensityB_;
    prevDir_ = &dirB_;
    
    LOG_INFO("TrailField", "Trail field initialized successfully");
    return true;
}

bool TrailField::createTextures(VkCommandPool commandPool, VkQueue graphicsQueue) {
    // Create empty storage images (no initial data)
    // We'll clear them in the first frame
    
    // Intensity textures (R16F)
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = resolution_;
    imageInfo.extent.height = resolution_;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    // Create intensity A
    VkImage imageA = VK_NULL_HANDLE;
    VmaAllocation allocationA = VK_NULL_HANDLE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &imageA, &allocationA, nullptr) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create intensity A image");
        return false;
    }
    
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = imageA;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    VkImageView viewA = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &viewA) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create intensity A view");
        vmaDestroyImage(allocator_, imageA, allocationA);
        return false;
    }
    
    intensityA_.setImageData(device_, allocator_, imageA, viewA, allocationA,
                             resolution_, resolution_, 1, 1, VK_FORMAT_R16_SFLOAT);
    
    // Create sampler for intensity A
    if (!intensityA_.createSampler(device_, VK_FILTER_LINEAR,
                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("TrailField", "Failed to create intensity sampler");
        return false;
    }
    
    // Create intensity B (same config)
    VkImage imageB = VK_NULL_HANDLE;
    VmaAllocation allocationB = VK_NULL_HANDLE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &imageB, &allocationB, nullptr) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create intensity B image");
        return false;
    }
    
    viewInfo.image = imageB;
    VkImageView viewB = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &viewB) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create intensity B view");
        vmaDestroyImage(allocator_, imageB, allocationB);
        return false;
    }
    
    intensityB_.setImageData(device_, allocator_, imageB, viewB, allocationB,
                             resolution_, resolution_, 1, 1, VK_FORMAT_R16_SFLOAT);
    
    if (!intensityB_.createSampler(device_, VK_FILTER_LINEAR,
                                   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("TrailField", "Failed to create intensity B sampler");
        return false;
    }
    
    // Direction textures (RG16F)
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
    
    VkImage imageDirA = VK_NULL_HANDLE;
    VmaAllocation allocationDirA = VK_NULL_HANDLE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &imageDirA, &allocationDirA, nullptr) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create dir A image");
        return false;
    }
    
    viewInfo.image = imageDirA;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    VkImageView viewDirA = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &viewDirA) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create dir A view");
        vmaDestroyImage(allocator_, imageDirA, allocationDirA);
        return false;
    }
    
    dirA_.setImageData(device_, allocator_, imageDirA, viewDirA, allocationDirA,
                       resolution_, resolution_, 1, 1, VK_FORMAT_R16G16_SFLOAT);
    
    if (!dirA_.createSampler(device_, VK_FILTER_LINEAR,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("TrailField", "Failed to create dir A sampler");
        return false;
    }
    
    VkImage imageDirB = VK_NULL_HANDLE;
    VmaAllocation allocationDirB = VK_NULL_HANDLE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &imageDirB, &allocationDirB, nullptr) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create dir B image");
        return false;
    }
    
    viewInfo.image = imageDirB;
    VkImageView viewDirB = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &viewDirB) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create dir B view");
        vmaDestroyImage(allocator_, imageDirB, allocationDirB);
        return false;
    }
    
    dirB_.setImageData(device_, allocator_, imageDirB, viewDirB, allocationDirB,
                       resolution_, resolution_, 1, 1, VK_FORMAT_R16G16_SFLOAT);
    
    if (!dirB_.createSampler(device_, VK_FILTER_LINEAR,
                             VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("TrailField", "Failed to create dir B sampler");
        return false;
    }
    
    LOG_INFO("TrailField", "Created ping-pong trail textures");
    return true;
}

bool TrailField::createDescriptors() {
    // Descriptor set layout (5 bindings)
    VkDescriptorSetLayoutBinding bindings[5] = {};
    
    // binding 0: prevIntensity (sampler)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // binding 1: prevDir (sampler)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // binding 2: outIntensity (storage image)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // binding 3: outDir (storage image)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // binding 4: events SSBO
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;
    
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create descriptor set layout");
        return false;
    }
    
    // Descriptor pool (2 sets for ping-pong)
    VkDescriptorPoolSize poolSizes[3] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 4;  // 2 samplers * 2 sets
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 4;  // 2 storage images * 2 sets
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 2;  // 1 SSBO * 2 sets
    
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 2;
    
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to create descriptor pool");
        return false;
    }
    
    // Allocate 2 descriptor sets
    descriptorSets_.resize(2);
    VkDescriptorSetLayout layouts[2] = {descriptorSetLayout_, descriptorSetLayout_};
    
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    
    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        LOG_ERROR("TrailField", "Failed to allocate descriptor sets");
        return false;
    }
    
    // Update descriptor sets for ping-pong configurations
    updateDescriptorSet(0);  // A→B
    updateDescriptorSet(1);  // B→A
    
    LOG_INFO("TrailField", "Created trail descriptors");
    return true;
}

void TrailField::updateDescriptorSet(uint32_t setIndex) {
    // Set 0: A reads, B writes
    // Set 1: B reads, A writes
    
    VulkanTexture* readIntensity = (setIndex == 0) ? &intensityA_ : &intensityB_;
    VulkanTexture* readDir = (setIndex == 0) ? &dirA_ : &dirB_;
    VulkanTexture* writeIntensity = (setIndex == 0) ? &intensityB_ : &intensityA_;
    VulkanTexture* writeDir = (setIndex == 0) ? &dirB_ : &dirA_;
    
    std::vector<VkWriteDescriptorSet> writes;
    
    // binding 0: prevIntensity
    VkDescriptorImageInfo prevIntensityInfo = {};
    prevIntensityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevIntensityInfo.imageView = readIntensity->getImageView();
    prevIntensityInfo.sampler = readIntensity->getSampler();
    
    VkWriteDescriptorSet write0 = {};
    write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write0.dstSet = descriptorSets_[setIndex];
    write0.dstBinding = 0;
    write0.dstArrayElement = 0;
    write0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write0.descriptorCount = 1;
    write0.pImageInfo = &prevIntensityInfo;
    writes.push_back(write0);
    
    // binding 1: prevDir
    VkDescriptorImageInfo prevDirInfo = {};
    prevDirInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevDirInfo.imageView = readDir->getImageView();
    prevDirInfo.sampler = readDir->getSampler();
    
    VkWriteDescriptorSet write1 = {};
    write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write1.dstSet = descriptorSets_[setIndex];
    write1.dstBinding = 1;
    write1.dstArrayElement = 0;
    write1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write1.descriptorCount = 1;
    write1.pImageInfo = &prevDirInfo;
    writes.push_back(write1);
    
    // binding 2: outIntensity
    VkDescriptorImageInfo outIntensityInfo = {};
    outIntensityInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outIntensityInfo.imageView = writeIntensity->getImageView();
    
    VkWriteDescriptorSet write2 = {};
    write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write2.dstSet = descriptorSets_[setIndex];
    write2.dstBinding = 2;
    write2.dstArrayElement = 0;
    write2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write2.descriptorCount = 1;
    write2.pImageInfo = &outIntensityInfo;
    writes.push_back(write2);
    
    // binding 3: outDir
    VkDescriptorImageInfo outDirInfo = {};
    outDirInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outDirInfo.imageView = writeDir->getImageView();
    
    VkWriteDescriptorSet write3 = {};
    write3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write3.dstSet = descriptorSets_[setIndex];
    write3.dstBinding = 3;
    write3.dstArrayElement = 0;
    write3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write3.descriptorCount = 1;
    write3.pImageInfo = &outDirInfo;
    writes.push_back(write3);
    
    // binding 4: events SSBO
    VkDescriptorBufferInfo eventsInfo = {};
    eventsInfo.buffer = eventsSSBO_->getBuffer();
    eventsInfo.offset = 0;
    eventsInfo.range = VK_WHOLE_SIZE;
    
    VkWriteDescriptorSet write4 = {};
    write4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write4.dstSet = descriptorSets_[setIndex];
    write4.dstBinding = 4;
    write4.dstArrayElement = 0;
    write4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write4.descriptorCount = 1;
    write4.pBufferInfo = &eventsInfo;
    writes.push_back(write4);
    
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), 
                          writes.data(), 0, nullptr);
}

bool TrailField::createPipeline() {
    // Push constant range
    VkPushConstantRange pushConstant = {};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = 128;  // TrailParams
    
    if (!updatePipeline_.create(device_, "shaders/compute/trail_update.comp.spv",
                                {descriptorSetLayout_}, &pushConstant)) {
        LOG_ERROR("TrailField", "Failed to create trail update pipeline");
        return false;
    }
    
    LOG_INFO("TrailField", "Created trail update compute pipeline");
    return true;
}

void TrailField::pushEvent(const TrailEvent& event) {
    if (eventsStaging_.size() < MAX_TRAIL_EVENTS) {
        eventsStaging_.push_back(event);
    }
}

void TrailField::clearEvents() {
    eventsStaging_.clear();
}

void TrailField::update(VkCommandBuffer cmd, float dtSeconds, const glm::vec2& newOrigin) {
    // Upload events to GPU
    if (!eventsStaging_.empty()) {
        VkDeviceSize uploadSize = sizeof(TrailEvent) * eventsStaging_.size();
        eventsSSBO_->upload(eventsStaging_.data(), uploadSize);
    }
    
    // Push constants
    struct TrailParams {
        glm::vec2 prevOrigin;
        glm::vec2 newOrigin;
        float worldSize;
        float invWorldSize;
        float dtSeconds;
        float relaxSeconds;
        uint32_t eventCount;
        uint32_t resolution;
        uint32_t _pad0;
        uint32_t _pad1;
    } params;
    
    params.prevOrigin = prevOrigin_;
    params.newOrigin = newOrigin;
    params.worldSize = worldSize_;
    params.invWorldSize = 1.0f / worldSize_;
    params.dtSeconds = dtSeconds;
    params.relaxSeconds = relaxSeconds_;
    params.eventCount = static_cast<uint32_t>(eventsStaging_.size());
    params.resolution = resolution_;
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, updatePipeline_.getPipeline());
    
    // Bind descriptor set (ping-pong)
    static uint32_t pingPong = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           updatePipeline_.getLayout(), 0, 1,
                           &descriptorSets_[pingPong], 0, nullptr);
    
    // Push constants
    vkCmdPushConstants(cmd, updatePipeline_.getLayout(),
                      VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    
    // Dispatch (8x8 workgroups)
    uint32_t groupsX = (resolution_ + 7) / 8;
    uint32_t groupsY = (resolution_ + 7) / 8;
    updatePipeline_.dispatch(cmd, groupsX, groupsY, 1);
    
    // Swap ping-pong
    pingPong = 1 - pingPong;
    
    if (pingPong == 0) {
        currentIntensity_ = &intensityA_;
        currentDir_ = &dirA_;
        prevIntensity_ = &intensityB_;
        prevDir_ = &dirB_;
    } else {
        currentIntensity_ = &intensityB_;
        currentDir_ = &dirB_;
        prevIntensity_ = &intensityA_;
        prevDir_ = &dirA_;
    }
    
    // Update origin for next frame
    prevOrigin_ = newOrigin;
    currentOrigin_ = newOrigin;
    
    // Clear events for next frame
    eventsStaging_.clear();
}

void TrailField::setRelaxSeconds(float seconds) {
    relaxSeconds_ = std::clamp(seconds, 5.0f, 20.0f);
}

} // namespace landscape

