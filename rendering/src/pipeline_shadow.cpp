/**
 * @file pipeline_shadow.cpp
 * @brief Shadow map generation pipeline implementation
 */

#include "rendering/pipeline_shadow.h"
#include "rendering/scene_manager.h"
#include "rendering/vulkan_mesh.h"
#include "rendering/vertex_formats.h"
#include "logging/logging.h"
#include <fstream>
#include <stdexcept>

namespace jupiter::rendering {

PipelineShadow::PipelineShadow(VkDevice device,
                               VkPhysicalDevice physicalDevice,
                               const PipelineConfig& config,
                               ResourcesShadow* resourcesShadow)
    : PipelineBase(device, config)
    , physicalDevice_(physicalDevice)
    , resourcesShadow_(resourcesShadow) {
    
    LOG_INFO("PipelineShadow", "Creating shadow pipeline");

    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createPipeline();
    createVoxelPipeline();

    LOG_INFO("PipelineShadow", "Shadow pipeline created successfully");
}

PipelineShadow::~PipelineShadow() {
    // Cleanup voxel pipeline
    if (voxelPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, voxelPipeline_, nullptr);
    }
    if (voxelPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, voxelPipelineLayout_, nullptr);
    }
    // Base class handles standard pipeline cleanup
}

void PipelineShadow::createDescriptorSetLayout() {
    // Binding 0: Shadow UBO
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow descriptor set layout");
    }
}

void PipelineShadow::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 2;  // One per frame in flight

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow descriptor pool");
    }
}

void PipelineShadow::createDescriptorSets() {
    shadowDescriptorSets_.resize(2);  // Frames in flight

    std::vector<VkDescriptorSetLayout> layouts(2, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &allocInfo, shadowDescriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate shadow descriptor sets");
    }

    // Update descriptor sets
    for (uint32_t i = 0; i < 2; i++) {
        VkDescriptorBufferInfo bufferInfo = resourcesShadow_->getUBO(i).getDescriptorInfo();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = shadowDescriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }
}

void PipelineShadow::createPipeline() {
    // Load shaders
    VkShaderModule vertShaderModule = loadShaderModule("shaders/shadow/depth.vert.spv");
    VkShaderModule fragShaderModule = loadShaderModule("shaders/shadow/depth.frag.spv");

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

    // Vertex input - position only for shadow pass
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 3;  // Position only
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(resourcesShadow_->getResolution());
    viewport.height = static_cast<float>(resourcesShadow_->getResolution());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {resourcesShadow_->getResolution(), resourcesShadow_->getResolution()};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer - depth bias for shadow acne prevention
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Front-face culling for shadow
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (none for depth-only)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    // Push constants for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline layout");
    }

    // Create pipeline
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
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = resourcesShadow_->getRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shadow pipeline");
    }

    // Cleanup shader modules
    vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragShaderModule, nullptr);
}

void PipelineShadow::createVoxelPipeline() {
    LOG_INFO("PipelineShadow", "Creating voxel shadow pipeline variant");

    // Load voxel-specific vertex shader and standard depth fragment shader
    VkShaderModule voxelVertShaderModule = loadShaderModule("shaders/shadow/depth_voxel.vert.spv");
    VkShaderModule fragShaderModule = loadShaderModule("shaders/shadow/depth.frag.spv");

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

    // Vertex input - VoxelVertexGPU format (8 bytes, two uint32)
    auto voxelVertexDesc = VoxelVertexGPU::getDescription();

    VkVertexInputBindingDescription bindingDescription = voxelVertexDesc.bindings[0];

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(voxelVertexDesc.attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = voxelVertexDesc.attributes.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(resourcesShadow_->getResolution());
    viewport.height = static_cast<float>(resourcesShadow_->getResolution());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {resourcesShadow_->getResolution(), resourcesShadow_->getResolution()};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer - depth bias for shadow acne prevention
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;  // Front-face culling for shadow
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (none for depth-only)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    // Voxel push constants (offset + scale)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VoxelShadowPushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &voxelPipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel shadow pipeline layout");
    }

    // Create pipeline
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
    pipelineInfo.layout = voxelPipelineLayout_;
    pipelineInfo.renderPass = resourcesShadow_->getRenderPass();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &voxelPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel shadow pipeline");
    }

    // Cleanup shader modules
    vkDestroyShaderModule(device_, voxelVertShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragShaderModule, nullptr);

    LOG_INFO("PipelineShadow", "Voxel shadow pipeline variant created successfully");
}

void PipelineShadow::updateShadow(const glm::vec3& lightPos,
                                   const glm::vec3& lightDir,
                                   const glm::vec3& targetPos) {
    // Update shadow UBO for both frames
    resourcesShadow_->updateShadowUBO(0, lightPos, lightDir, targetPos);
    resourcesShadow_->updateShadowUBO(1, lightPos, lightDir, targetPos);
}

void PipelineShadow::fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!sceneManager_) return;

    // Begin shadow render pass
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = resourcesShadow_->getRenderPass();
    renderPassInfo.framebuffer = resourcesShadow_->getFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {resourcesShadow_->getResolution(), resourcesShadow_->getResolution()};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor (shared by both pipelines)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(resourcesShadow_->getResolution());
    viewport.height = static_cast<float>(resourcesShadow_->getResolution());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {resourcesShadow_->getResolution(), resourcesShadow_->getResolution()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const auto& renderables = sceneManager_->getRenderables();

    // ========================================================================
    // Pass 1: Render standard geometry with standard pipeline
    // ========================================================================
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                           0, 1, &shadowDescriptorSets_[frameIndex], 0, nullptr);

    for (const auto& renderable : renderables) {
        if (!renderable.visible || !renderable.mesh || !renderable.mesh->isValid()) {
            continue;
        }

        // Skip voxel renderables that use compact format (rendered in pass 2)
        if (renderable.usesCompactVoxelFormat()) {
            continue;
        }

        // Push model matrix
        ShadowPushConstants pushConstants;
        pushConstants.model = renderable.transform;
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(ShadowPushConstants), &pushConstants);

        // Bind and draw mesh
        renderable.mesh->bind(cmd);
        renderable.mesh->draw(cmd);
    }

    // ========================================================================
    // Pass 2: Render voxel chunks with voxel pipeline (compact vertex format)
    // ========================================================================
    if (voxelPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline_);

        // Bind descriptor set for voxel pipeline
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipelineLayout_,
                               0, 1, &shadowDescriptorSets_[frameIndex], 0, nullptr);

        for (const auto& renderable : renderables) {
            if (!renderable.visible || !renderable.mesh || !renderable.mesh->isValid()) {
                continue;
            }

            // Only render voxel chunks with compact format
            if (!renderable.usesCompactVoxelFormat()) {
                continue;
            }

            // Voxel push constants (offset + scale)
            VoxelShadowPushConstants voxelPushConstants;
            voxelPushConstants.chunkOffset = glm::vec4(renderable.chunkOffset, 0.0f);
            voxelPushConstants.scale = glm::vec4(renderable.voxelScale, 0.0f);

            vkCmdPushConstants(cmd, voxelPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                              0, sizeof(VoxelShadowPushConstants), &voxelPushConstants);

            // Bind and draw mesh
            renderable.mesh->bind(cmd);
            renderable.mesh->draw(cmd);
        }
    }

    vkCmdEndRenderPass(cmd);
}

} // namespace jupiter::rendering

