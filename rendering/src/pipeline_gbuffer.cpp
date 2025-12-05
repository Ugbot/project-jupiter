/**
 * @file pipeline_gbuffer.cpp
 * @brief G-buffer geometry pass pipeline implementation
 */

#include "rendering/pipeline_gbuffer.h"
#include "rendering/scene_manager.h"
#include "rendering/vulkan_mesh.h"
#include "rendering/vertex_formats.h"
#include "rendering/resources_base.h"
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
                                  ResourcesGBuffer* resourcesGBuffer)
    : PipelineBase(device, config)
    , physicalDevice_(physicalDevice)
    , resourcesGBuffer_(resourcesGBuffer) {
    
    LOG_INFO("PipelineGBuffer", "Creating G-buffer pipeline");

    createUBOBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createPipeline();

    LOG_INFO("PipelineGBuffer", "G-buffer pipeline created successfully");
}

PipelineGBuffer::~PipelineGBuffer() {
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

    // Two color attachments for MRT (position + normal)
    std::array<VkPipelineColorBlendAttachmentState, 2> colorBlendAttachments{};
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

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
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

    // Begin G-buffer render pass
    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // Position (w=1 = background)
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};  // Normal
    clearValues[2].depthStencil = {1.0f, 0};            // Depth

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = resourcesGBuffer_->getGBufferRenderPass();
    renderPassInfo.framebuffer = resourcesGBuffer_->getGBufferFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {config.width, config.height};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Set viewport and scissor
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

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                           0, 1, &gBufferDescriptorSets_[frameIndex], 0, nullptr);

    // Render all scene geometry
    const auto& renderables = sceneManager_->getRenderables();
    for (const auto& renderable : renderables) {
        if (!renderable.visible || !renderable.mesh || !renderable.mesh->isValid()) {
            continue;
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

    vkCmdEndRenderPass(cmd);
}

} // namespace jupiter::rendering

