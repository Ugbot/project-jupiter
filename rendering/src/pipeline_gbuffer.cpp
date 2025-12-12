/**
 * @file pipeline_gbuffer.cpp
 * @brief G-buffer geometry pass pipeline implementation
 */

#include "rendering/pipeline_gbuffer.h"
#include "rendering/scene_manager.h"
#include "rendering/vulkan_mesh.h"
#include "rendering/vertex_formats.h"
#include "rendering/resources_base.h"
#include "rendering/material.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>
#include <stdexcept>

namespace jupiter::rendering {

namespace {

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

} // anonymous namespace

PipelineGBuffer::PipelineGBuffer(VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  const PipelineConfig& config,
                                  ResourcesGBuffer* resourcesGBuffer,
                                  MaterialSystem* materialSystem)
    : PipelineBase(device, config)
    , physicalDevice_(physicalDevice)
    , resourcesGBuffer_(resourcesGBuffer)
    , materialSystem_(materialSystem) {
    
    LOG_INFO("PipelineGBuffer", "Creating G-buffer pipeline (deferred rendering)");

    createUBOBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createPipeline();
    createVoxelPipeline();

    LOG_INFO("PipelineGBuffer", "G-buffer pipeline created successfully");
}

PipelineGBuffer::~PipelineGBuffer() {
    // Cleanup voxel pipeline
    if (voxelPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, voxelPipeline_, nullptr);
    }
    if (voxelPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, voxelPipelineLayout_, nullptr);
    }

    for (auto& ubo : cameraUBOs_) {
        if (ubo.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, ubo.buffer, nullptr);
        }
        if (ubo.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, ubo.memory, nullptr);
        }
    }
}

void PipelineGBuffer::createUBOBuffers() {
    cameraUBOs_.resize(2);  // Frames in flight

    VkDeviceSize bufferSize = sizeof(CameraUBO);

    for (uint32_t i = 0; i < 2; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &bufferInfo, nullptr, &cameraUBOs_[i].buffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create G-buffer camera UBO");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device_, cameraUBOs_[i].buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice_, memRequirements.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device_, &allocInfo, nullptr, &cameraUBOs_[i].memory) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate G-buffer camera UBO memory");
        }

        vkBindBufferMemory(device_, cameraUBOs_[i].buffer, cameraUBOs_[i].memory, 0);
        vkMapMemory(device_, cameraUBOs_[i].memory, 0, bufferSize, 0, &cameraUBOs_[i].mappedData);
        cameraUBOs_[i].size = bufferSize;
    }
}

void PipelineGBuffer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &cameraBinding;

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer descriptor set layout");
    }
}

void PipelineGBuffer::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer descriptor pool");
    }
}

void PipelineGBuffer::createDescriptorSets() {
    gBufferDescriptorSets_.resize(2);

    std::vector<VkDescriptorSetLayout> layouts(2, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &allocInfo, gBufferDescriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate G-buffer descriptor sets");
    }

    for (uint32_t i = 0; i < 2; i++) {
        VkDescriptorBufferInfo bufferInfo = cameraUBOs_[i].getDescriptorInfo();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = gBufferDescriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }
}

void PipelineGBuffer::createPipeline() {
    VkShaderModule vertShaderModule = loadShaderModule("shaders/gbuffer/gbuffer.vert.spv");
    VkShaderModule fragShaderModule = loadShaderModule("shaders/gbuffer/gbuffer.frag.spv");

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input - use Vertex3DLit format (same as PBR pipeline)
    auto vertexDesc = Vertex3DLit::getDescription();
    
    VkVertexInputBindingDescription bindingDescription = vertexDesc.bindings[0];

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDesc.attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexDesc.attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport and scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Five color attachments for MRT (position, normal, albedo, material, emissive)
    std::array<VkPipelineColorBlendAttachmentState, 5> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Push constants for model and normal matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GBufferPushConstants);

    // Descriptor set layouts: Set 0 = Camera, Set 1 = Material (if available)
    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.push_back(descriptorSetLayout_);  // Set 0: Camera UBO
    if (materialSystem_) {
        setLayouts.push_back(materialSystem_->getDescriptorSetLayout());  // Set 1: Material textures
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = resourcesGBuffer_->getGBufferRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create G-buffer pipeline");
    }

    vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragShaderModule, nullptr);
}

void PipelineGBuffer::createVoxelPipeline() {
    LOG_INFO("PipelineGBuffer", "Creating voxel pipeline variant");

    // Load voxel-specific vertex shader, reuse standard fragment shader
    VkShaderModule voxelVertShaderModule = loadShaderModule("shaders/gbuffer/gbuffer_voxel.vert.spv");
    VkShaderModule fragShaderModule = loadShaderModule("shaders/gbuffer/gbuffer.frag.spv");

    VkPipelineShaderStageCreateInfo voxelVertShaderStageInfo{};
    voxelVertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    voxelVertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    voxelVertShaderStageInfo.module = voxelVertShaderModule;
    voxelVertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {voxelVertShaderStageInfo, fragShaderStageInfo};

    // Vertex input - use VoxelVertexGPU format (8 bytes, two uint32)
    auto voxelVertexDesc = VoxelVertexGPU::getDescription();

    VkVertexInputBindingDescription bindingDescription = voxelVertexDesc.bindings[0];

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(voxelVertexDesc.attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = voxelVertexDesc.attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport and scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Five color attachments for MRT (same as standard pipeline)
    std::array<VkPipelineColorBlendAttachmentState, 5> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Voxel push constants (different from standard pipeline)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VoxelGBufferPushConstants);

    // Descriptor set layouts: Set 0 = Camera, Set 1 = Material (if available)
    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.push_back(descriptorSetLayout_);  // Set 0: Camera UBO
    if (materialSystem_) {
        setLayouts.push_back(materialSystem_->getDescriptorSetLayout());  // Set 1: Material textures
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &voxelPipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel G-buffer pipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = voxelPipelineLayout_;
    pipelineInfo.renderPass = resourcesGBuffer_->getGBufferRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &voxelPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel G-buffer pipeline");
    }

    vkDestroyShaderModule(device_, voxelVertShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragShaderModule, nullptr);

    LOG_INFO("PipelineGBuffer", "Voxel pipeline variant created successfully");
}

void PipelineGBuffer::onWindowResized(uint32_t width, uint32_t height) {
    // Resources handle their own resizing
}

void PipelineGBuffer::setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) {
    currentCameraUBO_ = ubo;
    std::memcpy(cameraUBOs_[frameIndex].mappedData, &ubo, sizeof(CameraUBO));
}

void PipelineGBuffer::fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!sceneManager_) return;

    const auto& config = resourcesGBuffer_->getConfig();

    // Begin G-buffer render pass (6 attachments: 5 color + 1 depth)
    std::array<VkClearValue, 6> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Position (w=1 = background)
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Normal
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Albedo + Metallic
    clearValues[3].color = {{1.0f, 1.0f, 0.0f, 0.0f}};  // Material (roughness=1, ao=1)
    clearValues[4].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Emissive
    clearValues[5].depthStencil = {1.0f, 0};            // Depth

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = resourcesGBuffer_->getGBufferRenderPass();
    renderPassInfo.framebuffer = resourcesGBuffer_->getGBufferFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {config.width, config.height};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor (shared by both pipelines)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(config.width);
    viewport.height = static_cast<float>(config.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {config.width, config.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const auto& renderables = sceneManager_->getRenderables();

    // ========================================================================
    // Pass 1: Render standard geometry with standard pipeline
    // ========================================================================
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Bind camera descriptor set (Set 0)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                           0, 1, &gBufferDescriptorSets_[frameIndex], 0, nullptr);

    for (const auto& renderable : renderables) {
        if (!renderable.visible || !renderable.mesh || !renderable.mesh->isValid()) {
            continue;
        }

        // Skip voxel renderables that use compact format (rendered in pass 2)
        if (renderable.usesCompactVoxelFormat()) {
            continue;
        }

        // Bind material descriptor set (Set 1) if available
        if (materialSystem_ && renderable.material) {
            VkDescriptorSet materialSet = renderable.material->getDescriptorSet();
            if (materialSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                                       1, 1, &materialSet, 0, nullptr);
            }
        }

        // Calculate model-view normal matrix
        GBufferPushConstants pushConstants;
        pushConstants.model = renderable.transform;
        glm::mat4 modelView = currentCameraUBO_.view * renderable.transform;
        pushConstants.normalMatrix = glm::inverseTranspose(modelView);

        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(GBufferPushConstants), &pushConstants);

        renderable.mesh->bind(cmd);
        renderable.mesh->draw(cmd);
    }

    // ========================================================================
    // Pass 2: Render voxel chunks with voxel pipeline (compact vertex format)
    // ========================================================================
    if (voxelPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline_);

        // Bind camera descriptor set for voxel pipeline
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipelineLayout_,
                               0, 1, &gBufferDescriptorSets_[frameIndex], 0, nullptr);

        for (const auto& renderable : renderables) {
            if (!renderable.visible || !renderable.mesh || !renderable.mesh->isValid()) {
                continue;
            }

            // Only render voxel chunks with compact format
            if (!renderable.usesCompactVoxelFormat()) {
                continue;
            }

            // Bind material descriptor set (Set 1) if available
            if (materialSystem_ && renderable.material) {
                VkDescriptorSet materialSet = renderable.material->getDescriptorSet();
                if (materialSet != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipelineLayout_,
                                           1, 1, &materialSet, 0, nullptr);
                }
            }

            // Voxel push constants (offset + scale)
            VoxelGBufferPushConstants voxelPushConstants;
            voxelPushConstants.chunkOffset = glm::vec4(renderable.chunkOffset, 0.0f);
            voxelPushConstants.scale = glm::vec4(renderable.voxelScale, 0.0f);

            vkCmdPushConstants(cmd, voxelPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                              0, sizeof(VoxelGBufferPushConstants), &voxelPushConstants);

            renderable.mesh->bind(cmd);
            renderable.mesh->draw(cmd);
        }
    }

    vkCmdEndRenderPass(cmd);
}

} // namespace jupiter::rendering

