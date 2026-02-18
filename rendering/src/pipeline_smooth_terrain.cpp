/**
 * @file pipeline_smooth_terrain.cpp
 * @brief Implementation of smooth terrain rendering pipeline
 */

#include "rendering/pipeline_smooth_terrain.h"
#include "logging/logging.h"

#include <fstream>
#include <cstring>

namespace jupiter {
namespace rendering {

// ============================================================================
// Lifecycle
// ============================================================================

PipelineSmoothTerrain::~PipelineSmoothTerrain() {
    destroy();
}

bool PipelineSmoothTerrain::initialize(VkDevice device,
                                        VkPhysicalDevice physicalDevice,
                                        VmaAllocator allocator,
                                        VkRenderPass renderPass,
                                        VkFormat colorFormat,
                                        VkFormat depthFormat) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    allocator_ = allocator;
    
    if (!createDescriptorSetLayout()) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create descriptor set layout");
        return false;
    }
    
    if (!createPipelineLayout()) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create pipeline layout");
        return false;
    }
    
    if (!createPipeline(renderPass, colorFormat, depthFormat)) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create pipeline");
        return false;
    }
    
    if (!createDescriptorPool()) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create descriptor pool");
        return false;
    }
    
    if (!createUniformBuffers()) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create uniform buffers");
        return false;
    }
    
    if (!createDescriptorSets()) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create descriptor sets");
        return false;
    }
    
    LOG_INFO("PipelineSmoothTerrain", "Smooth terrain pipeline initialized");
    return true;
}

void PipelineSmoothTerrain::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    
    vkDeviceWaitIdle(device_);
    
    // Destroy chunk buffers
    for (auto& chunk : chunkBuffers_) {
        if (chunk.vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, chunk.vertexBuffer, chunk.vertexAlloc);
        }
        if (chunk.indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, chunk.indexBuffer, chunk.indexAlloc);
        }
        chunk = {};
    }
    
    // Destroy uniform buffers
    for (auto& frame : frameUBOs_) {
        if (frame.cameraBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, frame.cameraBuffer, frame.cameraAlloc);
        }
        if (frame.lightBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, frame.lightBuffer, frame.lightAlloc);
        }
        frame = {};
    }
    
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    
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
    
    device_ = VK_NULL_HANDLE;
}

// ============================================================================
// Resource Creation
// ============================================================================

bool PipelineSmoothTerrain::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    
    // Camera UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Light UBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    return vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) == VK_SUCCESS;
}

bool PipelineSmoothTerrain::createPipelineLayout() {
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(SmoothTerrainPushConstant);
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    
    return vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) == VK_SUCCESS;
}

static std::vector<uint32_t> loadShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to open shader: %s", path.c_str());
        return {};
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return buffer;
}

bool PipelineSmoothTerrain::createPipeline(VkRenderPass renderPass,
                                            VkFormat colorFormat,
                                            VkFormat depthFormat) {
    (void)colorFormat;
    (void)depthFormat;
    
    // Load shaders
    auto vertCode = loadShaderFile("shaders/voxel/smooth_terrain.vert.spv");
    auto fragCode = loadShaderFile("shaders/voxel/smooth_terrain.frag.spv");
    
    if (vertCode.empty() || fragCode.empty()) {
        return false;
    }
    
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    
    moduleInfo.codeSize = vertCode.size() * sizeof(uint32_t);
    moduleInfo.pCode = vertCode.data();
    VkShaderModule vertModule;
    if (vkCreateShaderModule(device_, &moduleInfo, nullptr, &vertModule) != VK_SUCCESS) {
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create vertex shader module");
        return false;
    }
    
    moduleInfo.codeSize = fragCode.size() * sizeof(uint32_t);
    moduleInfo.pCode = fragCode.data();
    VkShaderModule fragModule;
    if (vkCreateShaderModule(device_, &moduleInfo, nullptr, &fragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        LOG_ERROR("PipelineSmoothTerrain", "Failed to create fragment shader module");
        return false;
    }
    
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";
    
    // Vertex input
    auto bindingDesc = SmoothTerrainVertex::getBindingDescription();
    auto attrDescs = SmoothTerrainVertex::getAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Enable back-face culling
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;
    
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                 nullptr, &pipeline_);
    
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    
    return result == VK_SUCCESS;
}

bool PipelineSmoothTerrain::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;  // Camera + Light per frame
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    return vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) == VK_SUCCESS;
}

bool PipelineSmoothTerrain::createUniformBuffers() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocResult;
    
    for (auto& frame : frameUBOs_) {
        // Camera buffer
        bufferInfo.size = sizeof(SmoothTerrainCameraUBO);
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &frame.cameraBuffer, &frame.cameraAlloc, &allocResult) != VK_SUCCESS) {
            return false;
        }
        frame.cameraMapped = allocResult.pMappedData;
        
        // Light buffer
        bufferInfo.size = sizeof(SmoothTerrainLightUBO);
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &frame.lightBuffer, &frame.lightAlloc, &allocResult) != VK_SUCCESS) {
            return false;
        }
        frame.lightMapped = allocResult.pMappedData;
    }
    
    return true;
}

bool PipelineSmoothTerrain::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout_);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    
    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        return false;
    }
    
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        std::array<VkDescriptorBufferInfo, 2> bufferInfos{};
        bufferInfos[0].buffer = frameUBOs_[i].cameraBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = sizeof(SmoothTerrainCameraUBO);
        
        bufferInfos[1].buffer = frameUBOs_[i].lightBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = sizeof(SmoothTerrainLightUBO);
        
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfos[0];
        
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].pBufferInfo = &bufferInfos[1];
        
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
    
    return true;
}

uint32_t PipelineSmoothTerrain::findMemoryType(uint32_t typeFilter,
                                                VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

// ============================================================================
// Chunk Management
// ============================================================================

bool PipelineSmoothTerrain::uploadChunkMesh(uint32_t chunkIndex,
                                             const void* vertices,
                                             uint32_t vertexCount,
                                             const uint32_t* indices,
                                             uint32_t indexCount,
                                             const glm::vec3& worldOffset) {
    if (chunkIndex >= MAX_CHUNKS || !vertices || vertexCount == 0) {
        return false;
    }
    
    auto& chunk = chunkBuffers_[chunkIndex];
    
    // Destroy existing buffers
    if (chunk.vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, chunk.vertexBuffer, chunk.vertexAlloc);
        chunk.vertexBuffer = VK_NULL_HANDLE;
    }
    if (chunk.indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, chunk.indexBuffer, chunk.indexAlloc);
        chunk.indexBuffer = VK_NULL_HANDLE;
    }
    
    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = vertexCount * sizeof(SmoothTerrainVertex);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    VmaAllocationInfo allocResult;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                       &chunk.vertexBuffer, &chunk.vertexAlloc, &allocResult) != VK_SUCCESS) {
        return false;
    }
    
    std::memcpy(allocResult.pMappedData, vertices, bufferInfo.size);
    chunk.vertexCount = vertexCount;
    
    // Create index buffer if provided
    if (indices && indexCount > 0) {
        bufferInfo.size = indexCount * sizeof(uint32_t);
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &chunk.indexBuffer, &chunk.indexAlloc, &allocResult) != VK_SUCCESS) {
            return false;
        }
        
        std::memcpy(allocResult.pMappedData, indices, bufferInfo.size);
        chunk.indexCount = indexCount;
    } else {
        chunk.indexCount = 0;
    }
    
    chunk.worldOffset = worldOffset;
    chunk.valid = true;
    
    return true;
}

void PipelineSmoothTerrain::clearChunkMesh(uint32_t chunkIndex) {
    if (chunkIndex >= MAX_CHUNKS) return;
    
    auto& chunk = chunkBuffers_[chunkIndex];
    
    if (chunk.vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, chunk.vertexBuffer, chunk.vertexAlloc);
    }
    if (chunk.indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, chunk.indexBuffer, chunk.indexAlloc);
    }
    
    chunk = {};
}

// ============================================================================
// Rendering
// ============================================================================

void PipelineSmoothTerrain::setCameraUBO(const SmoothTerrainCameraUBO& ubo, uint32_t frameIndex) {
    if (frameIndex < MAX_FRAMES_IN_FLIGHT && frameUBOs_[frameIndex].cameraMapped) {
        std::memcpy(frameUBOs_[frameIndex].cameraMapped, &ubo, sizeof(ubo));
    }
}

void PipelineSmoothTerrain::setLightUBO(const SmoothTerrainLightUBO& ubo) {
    for (auto& frame : frameUBOs_) {
        if (frame.lightMapped) {
            std::memcpy(frame.lightMapped, &ubo, sizeof(ubo));
        }
    }
}

void PipelineSmoothTerrain::fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (pipeline_ == VK_NULL_HANDLE) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &descriptorSets_[frameIndex], 0, nullptr);
    
    for (const auto& chunk : chunkBuffers_) {
        if (!chunk.valid || chunk.vertexCount == 0) continue;
        
        // Set push constant
        SmoothTerrainPushConstant pc;
        pc.chunkOffset = glm::vec4(chunk.worldOffset, 0.0f);
        pc.scale = glm::vec4(1.0f);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(pc), &pc);
        
        // Bind vertex buffer
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &chunk.vertexBuffer, &offset);
        
        if (chunk.indexCount > 0) {
            // Indexed draw
            vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
        } else {
            // Non-indexed draw
            vkCmdDraw(cmd, chunk.vertexCount, 1, 0, 0);
        }
    }
}

} // namespace rendering
} // namespace jupiter



