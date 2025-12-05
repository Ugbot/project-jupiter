/**
 * @file pipeline_clustered.cpp
 * @brief Implementation of clustered forward compute pipelines
 */

#include "rendering/pipeline_clustered.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace jupiter::rendering {

// ============================================================================
// Helper: Load shader module
// ============================================================================

static VkShaderModule loadShaderModuleFromFile(VkDevice device, const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module from: " + filename);
    }

    return shaderModule;
}

// ============================================================================
// PipelineAABBGenerator
// ============================================================================

PipelineAABBGenerator::~PipelineAABBGenerator() {
    destroy();
}

void PipelineAABBGenerator::create(VkDevice device, const ClusteredForwardConfig& config) {
    device_ = device;
    config_ = config;

    createDescriptorSetLayout();
    createPipelineLayout();
    createComputePipeline();
}

void PipelineAABBGenerator::destroy() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
}

void PipelineAABBGenerator::createDescriptorSetLayout() {
    // Binding 0: AABB output buffer (storage buffer)
    VkDescriptorSetLayoutBinding aabbBinding{};
    aabbBinding.binding = 0;
    aabbBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    aabbBinding.descriptorCount = 1;
    aabbBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &aabbBinding;

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create AABB generator descriptor set layout");
    }

    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create AABB generator descriptor pool");
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate AABB generator descriptor set");
    }
}

void PipelineAABBGenerator::createPipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(AABBGeneratorPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create AABB generator pipeline layout");
    }
}

void PipelineAABBGenerator::createComputePipeline() {
    // Note: In production, shader path would be configurable
    VkShaderModule compModule = loadShaderModuleFromFile(device_, "shaders/clustered/aabb_generator.comp.spv");

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_;

    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, compModule, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create AABB generator compute pipeline");
    }
}

void PipelineAABBGenerator::dispatch(VkCommandBuffer cmd,
                                     ResourcesClusteredForward& resources,
                                     const glm::mat4& inverseProjection,
                                     uint32_t screenWidth,
                                     uint32_t screenHeight) {
    // Update descriptor set with AABB buffer
    VkDescriptorBufferInfo bufferInfo = resources.getAABBDescriptor();
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &descriptorSet_, 0, nullptr);

    // Set push constants
    const auto& config = resources.getConfig();
    AABBGeneratorPushConstants pc{};
    pc.inverseProjection = inverseProjection;
    pc.clusterCountX = config.clusterCountX;
    pc.clusterCountY = config.clusterCountY;
    pc.clusterCountZ = config.clusterCountZ;
    pc.zNear = config.zNear;
    pc.zFar = config.zFar;
    pc.screenWidth = screenWidth;
    pc.screenHeight = screenHeight;

    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(AABBGeneratorPushConstants), &pc);

    // Dispatch one workgroup per cluster
    vkCmdDispatch(cmd, config.clusterCountX, config.clusterCountY, config.clusterCountZ);

    // Memory barrier for AABB buffer
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = resources.getAABBBuffer().buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, nullptr, 1, &barrier, 0, nullptr);

    resources.clearAABBDirty();
}

// ============================================================================
// PipelineLightCulling
// ============================================================================

PipelineLightCulling::~PipelineLightCulling() {
    destroy();
}

void PipelineLightCulling::create(VkDevice device, const ClusteredForwardConfig& config) {
    device_ = device;
    config_ = config;

    createDescriptorSetLayout();
    createPipelineLayout();
    createComputePipeline();
}

void PipelineLightCulling::destroy() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
}

void PipelineLightCulling::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};

    // Binding 0: Cluster AABBs (read)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Lights (read)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Atomic counter
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Light cells (write)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Light indices (write)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;  // 5 bindings
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culling descriptor set layout");
    }

    // Create descriptor pool (one set per frame in flight)
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 5 * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culling descriptor pool");
    }

    // Allocate descriptor sets
    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
    layouts.fill(descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate light culling descriptor sets");
    }
}

void PipelineLightCulling::createPipelineLayout() {
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LightCullingPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culling pipeline layout");
    }
}

void PipelineLightCulling::createComputePipeline() {
    VkShaderModule compModule = loadShaderModuleFromFile(device_, "shaders/clustered/light_culling.comp.spv");

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_;

    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, compModule, nullptr);

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create light culling compute pipeline");
    }
}

void PipelineLightCulling::updateDescriptors(ResourcesClusteredForward& clusteredResources,
                                             ResourcesLight& lightResources,
                                             uint32_t frameIndex) {
    std::array<VkDescriptorBufferInfo, 5> bufferInfos{};
    
    // Cluster AABBs
    bufferInfos[0] = clusteredResources.getAABBDescriptor();
    // Lights
    bufferInfos[1] = lightResources.getDescriptor();
    // Atomic counter
    bufferInfos[2] = clusteredResources.getGlobalIndexCountBuffer(frameIndex).getDescriptorInfo();
    // Light cells
    bufferInfos[3] = clusteredResources.getLightCellsDescriptor();
    // Light indices
    bufferInfos[4] = clusteredResources.getLightIndicesDescriptor();

    std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
    for (size_t i = 0; i < 5; ++i) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSets_[frameIndex];
        descriptorWrites[i].dstBinding = static_cast<uint32_t>(i);
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pBufferInfo = &bufferInfos[i];
    }

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()),
                          descriptorWrites.data(), 0, nullptr);
}

void PipelineLightCulling::dispatch(VkCommandBuffer cmd,
                                    ResourcesClusteredForward& clusteredResources,
                                    ResourcesLight& lightResources,
                                    const glm::mat4& viewMatrix,
                                    uint32_t frameIndex) {
    // Reset atomic counter
    clusteredResources.resetGlobalIndexCount(frameIndex);

    // Update descriptors
    updateDescriptors(clusteredResources, lightResources, frameIndex);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_,
                           0, 1, &descriptorSets_[frameIndex], 0, nullptr);

    // Set push constants
    const auto& config = clusteredResources.getConfig();
    LightCullingPushConstants pc{};
    pc.viewMatrix = viewMatrix;
    pc.clusterCountX = config.clusterCountX;
    pc.clusterCountY = config.clusterCountY;
    pc.clusterCountZ = config.clusterCountZ;
    pc.lightCount = lightResources.getLightCount();
    pc.maxLightsPerCluster = config.maxLightsPerCluster;

    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(LightCullingPushConstants), &pc);

    // Dispatch one workgroup per cluster
    vkCmdDispatch(cmd, config.clusterCountX, config.clusterCountY, config.clusterCountZ);

    // Memory barriers for output buffers
    std::array<VkBufferMemoryBarrier, 2> barriers{};
    
    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].buffer = clusteredResources.getLightCellsBuffer().buffer;
    barriers[0].offset = 0;
    barriers[0].size = VK_WHOLE_SIZE;

    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].buffer = clusteredResources.getLightIndicesBuffer().buffer;
    barriers[1].offset = 0;
    barriers[1].size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr,
                        static_cast<uint32_t>(barriers.size()), barriers.data(),
                        0, nullptr);
}

// ============================================================================
// ClusteredForwardPipelines
// ============================================================================

void ClusteredForwardPipelines::create(VkDevice device, VkPhysicalDevice physicalDevice,
                                       const ClusteredForwardConfig& config,
                                       uint32_t maxLights) {
    device_ = device;
    config_ = config;

    // Create resources
    clusteredResources_.create(device, physicalDevice, config);
    lightResources_.create(device, physicalDevice, maxLights);

    // Create compute pipelines
    aabbGenerator_.create(device, config);
    lightCulling_.create(device, config);
}

void ClusteredForwardPipelines::destroy() {
    aabbGenerator_.destroy();
    lightCulling_.destroy();
    clusteredResources_.destroy();
    lightResources_.destroy();
}

void ClusteredForwardPipelines::execute(VkCommandBuffer cmd,
                                        const glm::mat4& viewMatrix,
                                        const glm::mat4& inverseProjection,
                                        uint32_t screenWidth,
                                        uint32_t screenHeight,
                                        uint32_t frameIndex) {
    // Generate AABBs if needed (typically only on resize or first frame)
    if (clusteredResources_.isAABBDirty()) {
        aabbGenerator_.dispatch(cmd, clusteredResources_, inverseProjection,
                               screenWidth, screenHeight);
    }

    // Cull lights every frame
    lightCulling_.dispatch(cmd, clusteredResources_, lightResources_,
                          viewMatrix, frameIndex);
}

} // namespace jupiter::rendering

