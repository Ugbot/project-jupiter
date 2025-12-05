/**
 * @file pipeline_base.cpp
 * @brief Implementation of PipelineBase class
 */

#include "rendering/pipeline_base.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace jupiter::rendering {

// ============================================================================
// Construction/Destruction
// ============================================================================

PipelineBase::PipelineBase(VkDevice device, const PipelineConfig& config)
    : device_(device)
    , config_(config) {
}

PipelineBase::~PipelineBase() {
    // Destroy Vulkan resources
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }

    // Destroy framebuffers
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }

    // Destroy camera UBOs
    for (size_t i = 0; i < cameraUBOBuffers_.size(); ++i) {
        if (cameraUBOBuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, cameraUBOBuffers_[i], nullptr);
        }
        if (cameraUBOMemory_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device_, cameraUBOMemory_[i], nullptr);
        }
    }
}

// ============================================================================
// Virtual Methods
// ============================================================================

void PipelineBase::onWindowResized(uint32_t width, uint32_t height) {
    config_.viewportWidth = width;
    config_.viewportHeight = height;
    // Derived classes should recreate framebuffers if needed
}

void PipelineBase::setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) {
    if (frameIndex >= cameraUBOMemory_.size()) return;
    
    void* data;
    vkMapMemory(device_, cameraUBOMemory_[frameIndex], 0, sizeof(CameraUBO), 0, &data);
    std::memcpy(data, &ubo, sizeof(CameraUBO));
    vkUnmapMemory(device_, cameraUBOMemory_[frameIndex]);
}

// ============================================================================
// Helper Methods
// ============================================================================

void PipelineBase::createPipelineLayout(VkDescriptorSetLayout dsLayout,
                                        uint32_t pushConstantSize,
                                        VkShaderStageFlags pushConstantStages) {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    
    if (dsLayout != VK_NULL_HANDLE) {
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &dsLayout;
    }

    VkPushConstantRange pushConstantRange{};
    if (pushConstantSize > 0) {
        pushConstantRange.stageFlags = pushConstantStages;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantSize;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
    }

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }
}

void PipelineBase::createGraphicsPipeline(VkRenderPass renderPass,
                                          const std::vector<std::string>& shaderFiles,
                                          const VkPipelineVertexInputStateCreateInfo* vertexInput) {
    if (shaderFiles.size() < 2) {
        throw std::runtime_error("Graphics pipeline requires at least vertex and fragment shaders");
    }

    // Load shaders
    VkShaderModule vertModule = loadShaderModule(shaderFiles[0]);
    VkShaderModule fragModule = loadShaderModule(shaderFiles[1]);

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Default vertex input if none provided
    VkPipelineVertexInputStateCreateInfo defaultVertexInput{};
    defaultVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    const VkPipelineVertexInputStateCreateInfo* actualVertexInput = 
        vertexInput ? vertexInput : &defaultVertexInput;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor (dynamic state)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = config_.cullMode;
    rasterizer.frontFace = config_.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = config_.msaaSamples;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config_.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config_.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = config_.blendEnable ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = actualVertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, fragModule, nullptr);
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
}

void PipelineBase::createComputePipeline(const std::string& shaderFile) {
    VkShaderModule compModule = loadShaderModule(shaderFile);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_;

    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                 nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, compModule, nullptr);
        throw std::runtime_error("Failed to create compute pipeline");
    }

    vkDestroyShaderModule(device_, compModule, nullptr);
}

VkShaderModule PipelineBase::loadShaderModule(const std::string& filename) {
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
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module from: " + filename);
    }

    return shaderModule;
}

void PipelineBase::bindPipeline(VkCommandBuffer cmd) {
    VkPipelineBindPoint bindPoint = isCompute() ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindPipeline(cmd, bindPoint, pipeline_);
}

void PipelineBase::beginRenderPass(VkCommandBuffer cmd, VkFramebuffer framebuffer,
                                   const VkClearValue* clearValues, uint32_t clearValueCount) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {config_.viewportWidth, config_.viewportHeight};
    renderPassInfo.clearValueCount = clearValueCount;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void PipelineBase::endRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

void PipelineBase::setViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

} // namespace jupiter::rendering

