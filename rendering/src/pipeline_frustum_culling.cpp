/**
 * @file pipeline_frustum_culling.cpp
 * @brief Implementation of GPU frustum culling compute pipeline
 */

#include "rendering/pipeline_frustum_culling.h"
#include "rendering/indirect_draw_buffer.h"
#include "vulkan_backend.h"
#include "logging/logging.h"

#include <fstream>
#include <vector>

namespace jupiter::rendering {

bool PipelineFrustumCulling::initialize(VkDevice device, VmaAllocator allocator,
                                        const std::string& shaderPath) {
    device_ = device;
    allocator_ = allocator;

    if (!createFrustumUBOs()) {
        LOG_ERROR("FrustumCulling", "Failed to create frustum UBOs");
        return false;
    }

    if (!createDescriptorSetLayout()) {
        LOG_ERROR("FrustumCulling", "Failed to create descriptor set layout");
        return false;
    }

    if (!createDescriptorPool()) {
        LOG_ERROR("FrustumCulling", "Failed to create descriptor pool");
        return false;
    }

    if (!createDescriptorSets()) {
        LOG_ERROR("FrustumCulling", "Failed to create descriptor sets");
        return false;
    }

    if (!createPipelineLayout()) {
        LOG_ERROR("FrustumCulling", "Failed to create pipeline layout");
        return false;
    }

    if (!createPipeline(shaderPath)) {
        LOG_ERROR("FrustumCulling", "Failed to create compute pipeline");
        return false;
    }

    LOG_INFO("FrustumCulling", "Initialized GPU frustum culling pipeline");
    return true;
}

void PipelineFrustumCulling::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    if (shaderModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, shaderModule_, nullptr);
        shaderModule_ = VK_NULL_HANDLE;
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }

    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    for (auto& ubo : frustumUBOs_) {
        if (ubo) {
            ubo->destroy();
            ubo.reset();
        }
    }

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    boundBuffer_ = nullptr;
}

bool PipelineFrustumCulling::createFrustumUBOs() {
    VkDeviceSize uboSize = sizeof(GPUFrustum);

    for (uint32_t i = 0; i < CULLING_FRAMES_IN_FLIGHT; ++i) {
        frustumUBOs_[i] = std::make_unique<vulkan::VulkanBuffer>();
        if (!frustumUBOs_[i]->create(allocator_, uboSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU,
                VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
            return false;
        }
    }
    return true;
}

bool PipelineFrustumCulling::createDescriptorSetLayout() {
    // Binding 0: Frustum UBO
    // Binding 1: AABB buffer (SSBO, read)
    // Binding 2: Indirect command buffer (SSBO, read-write)

    VkDescriptorSetLayoutBinding bindings[3] = {};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    return vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                       &descriptorSetLayout_) == VK_SUCCESS;
}

bool PipelineFrustumCulling::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = CULLING_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = CULLING_FRAMES_IN_FLIGHT * 2;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = CULLING_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    return vkCreateDescriptorPool(device_, &poolInfo, nullptr,
                                  &descriptorPool_) == VK_SUCCESS;
}

bool PipelineFrustumCulling::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(CULLING_FRAMES_IN_FLIGHT,
                                               descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = CULLING_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    return vkAllocateDescriptorSets(device_, &allocInfo,
                                    descriptorSets_.data()) == VK_SUCCESS;
}

bool PipelineFrustumCulling::createPipelineLayout() {
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;

    return vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                                  &pipelineLayout_) == VK_SUCCESS;
}

bool PipelineFrustumCulling::loadShader(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("FrustumCulling", "Cannot open shader: %s", path.c_str());
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> code(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = fileSize;
    createInfo.pCode = code.data();

    return vkCreateShaderModule(device_, &createInfo, nullptr,
                                &shaderModule_) == VK_SUCCESS;
}

bool PipelineFrustumCulling::createPipeline(const std::string& shaderPath) {
    if (!loadShader(shaderPath)) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule_;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_;

    return vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                    nullptr, &pipeline_) == VK_SUCCESS;
}

void PipelineFrustumCulling::updateFrustum(const glm::mat4& viewProj,
                                           uint32_t frameIndex) {
    GPUFrustum frustum = extractFrustum(viewProj);
    frustumUBOs_[frameIndex]->upload(&frustum, sizeof(GPUFrustum));
}

void PipelineFrustumCulling::bindIndirectBuffer(IndirectDrawBuffer& indirectBuffer,
                                                uint32_t frameIndex) {
    if (&indirectBuffer != boundBuffer_) {
        updateDescriptorSet(indirectBuffer, frameIndex);
        boundBuffer_ = &indirectBuffer;
    }
}

void PipelineFrustumCulling::updateDescriptorSet(IndirectDrawBuffer& buffer,
                                                  uint32_t frameIndex) {
    VkDescriptorBufferInfo frustumInfo = {};
    frustumInfo.buffer = frustumUBOs_[frameIndex]->getBuffer();
    frustumInfo.offset = 0;
    frustumInfo.range = sizeof(GPUFrustum);

    VkDescriptorBufferInfo aabbInfo = buffer.getAABBBufferInfo();
    VkDescriptorBufferInfo commandInfo = buffer.getCommandBufferInfo();

    VkWriteDescriptorSet writes[3] = {};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSets_[frameIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &frustumInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSets_[frameIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &aabbInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSets_[frameIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &commandInfo;

    vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
}

void PipelineFrustumCulling::execute(VkCommandBuffer cmd,
                                     IndirectDrawBuffer& indirectBuffer,
                                     uint32_t frameIndex) {
    if (!isInitialized() || indirectBuffer.getDrawCount() == 0) {
        return;
    }

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipelineLayout_, 0, 1,
                           &descriptorSets_[frameIndex], 0, nullptr);

    // Dispatch one invocation per draw command
    vkCmdDispatch(cmd, indirectBuffer.getDrawCount(), 1, 1);

    // Barrier: ensure compute writes complete before indirect draw reads
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = indirectBuffer.getCommandBuffer();
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr);
}

} // namespace jupiter::rendering
