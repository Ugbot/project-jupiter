/**
 * @file pipeline_debug.cpp
 * @brief Debug visualization pipeline implementation
 */

#include "rendering/pipeline_debug.h"
#include "vulkan_backend.h"
#include "logging/logging.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace jupiter::rendering {

// Embedded SPIR-V for debug shaders (simple line rendering)
// These are minimal shaders that just transform vertices and output solid color

// debug.vert - transforms position by push constant MVP
static const uint32_t debugVertSpv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000028, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0009000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00000014,
    0x00000020, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00060005, 0x00000007, 0x505f6c67, 0x65567265,
    0x78657472, 0x00000000, 0x00060006, 0x00000007, 0x00000000, 0x505f6c67,
    0x7469736f, 0x006e6f69, 0x00070006, 0x00000007, 0x00000001, 0x505f6c67,
    0x746e696f, 0x657a6953, 0x00000000, 0x00070006, 0x00000007, 0x00000002,
    0x435f6c67, 0x4470696c, 0x61747369, 0x0065636e, 0x00070006, 0x00000007,
    0x00000003, 0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e, 0x00030005,
    0x00000009, 0x00000000, 0x00050005, 0x0000000b, 0x6f506e69, 0x69746973,
    0x00006e6f, 0x00050005, 0x00000012, 0x68737550, 0x736e6f43, 0x00007374,
    0x00050006, 0x00000012, 0x00000000, 0x4d70766d, 0x00007461, 0x00030005,
    0x00000014, 0x00006370, 0x00050005, 0x00000020, 0x43747561, 0x726f6c6f,
    0x00000000, 0x00050005, 0x00000022, 0x6f436e69, 0x00726f6c, 0x00050048,
    0x00000007, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x00000007,
    0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x00000007, 0x00000002,
    0x0000000b, 0x00000003, 0x00050048, 0x00000007, 0x00000003, 0x0000000b,
    0x00000004, 0x00030047, 0x00000007, 0x00000002, 0x00040047, 0x0000000b,
    0x0000001e, 0x00000000, 0x00040048, 0x00000012, 0x00000000, 0x00000005,
    0x00050048, 0x00000012, 0x00000000, 0x00000023, 0x00000000, 0x00050048,
    0x00000012, 0x00000000, 0x00000007, 0x00000010, 0x00030047, 0x00000012,
    0x00000002, 0x00040047, 0x00000020, 0x0000001e, 0x00000000, 0x00040047,
    0x00000022, 0x0000001e, 0x00000001, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000008, 0x00000006, 0x00000004, 0x00040015, 0x0000000a, 0x00000020,
    0x00000000, 0x0004002b, 0x0000000a, 0x0000000c, 0x00000001, 0x0004001c,
    0x0000000d, 0x00000006, 0x0000000c, 0x0006001e, 0x00000007, 0x00000008,
    0x00000006, 0x0000000d, 0x0000000d, 0x00040020, 0x0000000e, 0x00000003,
    0x00000007, 0x0004003b, 0x0000000e, 0x00000009, 0x00000003, 0x00040017,
    0x00000010, 0x00000006, 0x00000003, 0x00040020, 0x00000011, 0x00000001,
    0x00000010, 0x0004003b, 0x00000011, 0x0000000b, 0x00000001, 0x00040018,
    0x00000013, 0x00000008, 0x00000004, 0x0003001e, 0x00000012, 0x00000013,
    0x00040020, 0x00000015, 0x00000009, 0x00000012, 0x0004003b, 0x00000015,
    0x00000014, 0x00000009, 0x00040015, 0x00000016, 0x00000020, 0x00000001,
    0x0004002b, 0x00000016, 0x00000017, 0x00000000, 0x00040020, 0x00000018,
    0x00000009, 0x00000013, 0x0004002b, 0x00000006, 0x0000001c, 0x3f800000,
    0x00040020, 0x0000001e, 0x00000003, 0x00000008, 0x0004003b, 0x0000001e,
    0x00000020, 0x00000003, 0x00040020, 0x00000021, 0x00000001, 0x00000008,
    0x0004003b, 0x00000021, 0x00000022, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d,
    0x00000010, 0x00000019, 0x0000000b, 0x00050041, 0x00000018, 0x0000001a,
    0x00000014, 0x00000017, 0x0004003d, 0x00000013, 0x0000001b, 0x0000001a,
    0x00050051, 0x00000006, 0x0000001d, 0x00000019, 0x00000000, 0x00050051,
    0x00000006, 0x0000001f, 0x00000019, 0x00000001, 0x00050051, 0x00000006,
    0x00000023, 0x00000019, 0x00000002, 0x00070050, 0x00000008, 0x00000024,
    0x0000001d, 0x0000001f, 0x00000023, 0x0000001c, 0x00050091, 0x00000008,
    0x00000025, 0x0000001b, 0x00000024, 0x00050041, 0x0000001e, 0x00000026,
    0x00000009, 0x00000017, 0x0003003e, 0x00000026, 0x00000025, 0x0004003d,
    0x00000008, 0x00000027, 0x00000022, 0x0003003e, 0x00000020, 0x00000027,
    0x000100fd, 0x00010038
};

// debug.frag - outputs the interpolated color
static const uint32_t debugFragSpv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x0000000d, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x4374756f,
    0x726f6c6f, 0x00000000, 0x00040005, 0x0000000b, 0x6f6c6f63, 0x00000072,
    0x00040047, 0x00000009, 0x0000001e, 0x00000000, 0x00040047, 0x0000000b,
    0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007,
    0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x00040020, 0x0000000a,
    0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
    0x00000005, 0x0004003d, 0x00000007, 0x0000000c, 0x0000000b, 0x0003003e,
    0x00000009, 0x0000000c, 0x000100fd, 0x00010038
};

PipelineDebug::~PipelineDebug() {
    destroy();
}

bool PipelineDebug::initialize(VkDevice device, VmaAllocator allocator,
                               VkRenderPass renderPass, VkFormat depthFormat,
                               uint32_t width, uint32_t height) {
    device_ = device;
    allocator_ = allocator;

    // Create vertex buffer
    vertexBuffer_ = std::make_unique<vulkan::VulkanBuffer>();
    VkDeviceSize bufferSize = MAX_DEBUG_VERTICES * sizeof(DebugVertex);
    if (!vertexBuffer_->create(allocator_, bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        LOG_ERROR("Debug", "Failed to create vertex buffer");
        return false;
    }

    vertices_.reserve(MAX_DEBUG_VERTICES);

    if (!createShaders()) {
        LOG_ERROR("Debug", "Failed to create shaders");
        return false;
    }

    if (!createPipeline(renderPass, depthFormat)) {
        LOG_ERROR("Debug", "Failed to create pipeline");
        return false;
    }

    LOG_INFO("Debug", "Debug visualization pipeline initialized");
    return true;
}

void PipelineDebug::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    if (vertShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, vertShader_, nullptr);
        vertShader_ = VK_NULL_HANDLE;
    }

    if (fragShader_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, fragShader_, nullptr);
        fragShader_ = VK_NULL_HANDLE;
    }

    if (vertexBuffer_) {
        vertexBuffer_->destroy();
        vertexBuffer_.reset();
    }

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

bool PipelineDebug::createShaders() {
    VkShaderModuleCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertInfo.codeSize = sizeof(debugVertSpv);
    vertInfo.pCode = debugVertSpv;

    if (vkCreateShaderModule(device_, &vertInfo, nullptr, &vertShader_) != VK_SUCCESS) {
        return false;
    }

    VkShaderModuleCreateInfo fragInfo = {};
    fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragInfo.codeSize = sizeof(debugFragSpv);
    fragInfo.pCode = debugFragSpv;

    if (vkCreateShaderModule(device_, &fragInfo, nullptr, &fragShader_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool PipelineDebug::createPipeline(VkRenderPass renderPass, VkFormat depthFormat) {
    // Push constant range for MVP matrix
    VkPushConstantRange pushConstant = {};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShader_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragShader_;
    stages[1].pName = "main";

    // Vertex input
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(DebugVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2] = {};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(DebugVertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(DebugVertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    // Input assembly - lines
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    // Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterization = {};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Color blending (alpha blend)
    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    // Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    return vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) == VK_SUCCESS;
}

void PipelineDebug::setViewProjection(const glm::mat4& viewProj) {
    viewProj_ = viewProj;
}

void PipelineDebug::beginFrame() {
    vertices_.clear();
}

void PipelineDebug::addVertex(const glm::vec3& pos, const glm::vec4& color) {
    if (vertices_.size() < MAX_DEBUG_VERTICES) {
        vertices_.push_back({pos, color});
    }
}

void PipelineDebug::drawLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
    addVertex(start, color);
    addVertex(end, color);
}

void PipelineDebug::drawAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
    // 12 edges of the box
    // Bottom face
    drawLine(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z), color);
    drawLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color);
    drawLine(glm::vec3(max.x, min.y, max.z), glm::vec3(min.x, min.y, max.z), color);
    drawLine(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, min.y, min.z), color);
    // Top face
    drawLine(glm::vec3(min.x, max.y, min.z), glm::vec3(max.x, max.y, min.z), color);
    drawLine(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z), color);
    drawLine(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z), color);
    drawLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z), color);
    // Vertical edges
    drawLine(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, max.y, min.z), color);
    drawLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color);
    drawLine(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z), color);
    drawLine(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, max.y, max.z), color);
}

void PipelineDebug::drawFrustum(const glm::vec3 corners[8], const glm::vec4& color) {
    // Near face (0-3)
    drawLine(corners[0], corners[1], color);
    drawLine(corners[1], corners[2], color);
    drawLine(corners[2], corners[3], color);
    drawLine(corners[3], corners[0], color);
    // Far face (4-7)
    drawLine(corners[4], corners[5], color);
    drawLine(corners[5], corners[6], color);
    drawLine(corners[6], corners[7], color);
    drawLine(corners[7], corners[4], color);
    // Connecting edges
    drawLine(corners[0], corners[4], color);
    drawLine(corners[1], corners[5], color);
    drawLine(corners[2], corners[6], color);
    drawLine(corners[3], corners[7], color);
}

void PipelineDebug::drawFrustumFromMatrix(const glm::mat4& invViewProj, const glm::vec4& color) {
    // NDC corners
    glm::vec4 ndcCorners[8] = {
        // Near plane
        {-1, -1, 0, 1},  // BL
        { 1, -1, 0, 1},  // BR
        { 1,  1, 0, 1},  // TR
        {-1,  1, 0, 1},  // TL
        // Far plane
        {-1, -1, 1, 1},
        { 1, -1, 1, 1},
        { 1,  1, 1, 1},
        {-1,  1, 1, 1}
    };

    glm::vec3 worldCorners[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec4 world = invViewProj * ndcCorners[i];
        worldCorners[i] = glm::vec3(world) / world.w;
    }

    drawFrustum(worldCorners, color);
}

void PipelineDebug::drawSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);

    // XY circle
    for (int i = 0; i < segments; ++i) {
        float a1 = static_cast<float>(i) * step;
        float a2 = static_cast<float>(i + 1) * step;
        glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, std::sin(a1) * radius, 0.0f);
        glm::vec3 p2 = center + glm::vec3(std::cos(a2) * radius, std::sin(a2) * radius, 0.0f);
        drawLine(p1, p2, color);
    }

    // XZ circle
    for (int i = 0; i < segments; ++i) {
        float a1 = static_cast<float>(i) * step;
        float a2 = static_cast<float>(i + 1) * step;
        glm::vec3 p1 = center + glm::vec3(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
        glm::vec3 p2 = center + glm::vec3(std::cos(a2) * radius, 0.0f, std::sin(a2) * radius);
        drawLine(p1, p2, color);
    }

    // YZ circle
    for (int i = 0; i < segments; ++i) {
        float a1 = static_cast<float>(i) * step;
        float a2 = static_cast<float>(i + 1) * step;
        glm::vec3 p1 = center + glm::vec3(0.0f, std::cos(a1) * radius, std::sin(a1) * radius);
        glm::vec3 p2 = center + glm::vec3(0.0f, std::cos(a2) * radius, std::sin(a2) * radius);
        drawLine(p1, p2, color);
    }
}

void PipelineDebug::drawGrid(float size, float spacing, const glm::vec4& color) {
    int lines = static_cast<int>(size / spacing);
    float halfSize = size * 0.5f;

    for (int i = -lines; i <= lines; ++i) {
        float pos = static_cast<float>(i) * spacing;

        // Determine color (axis lines are colored)
        glm::vec4 lineColor = color;
        if (i == 0) {
            // X axis line (red)
            drawLine(glm::vec3(-halfSize, 0, 0), glm::vec3(halfSize, 0, 0), config_.gridAxisColorX);
            // Z axis line (blue)
            drawLine(glm::vec3(0, 0, -halfSize), glm::vec3(0, 0, halfSize), config_.gridAxisColorZ);
        } else {
            // Regular grid lines
            drawLine(glm::vec3(-halfSize, 0, pos), glm::vec3(halfSize, 0, pos), lineColor);
            drawLine(glm::vec3(pos, 0, -halfSize), glm::vec3(pos, 0, halfSize), lineColor);
        }
    }
}

void PipelineDebug::endFrame(VkCommandBuffer cmd) {
    if (vertices_.empty()) return;

    // Upload vertex data
    vertexBuffer_->upload(vertices_.data(), vertices_.size() * sizeof(DebugVertex));
}

void PipelineDebug::fillCommandBuffer(VkCommandBuffer cmd) {
    if (!isInitialized() || vertices_.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    // Push MVP matrix
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                      sizeof(glm::mat4), &viewProj_);

    // Bind vertex buffer
    VkBuffer buffers[] = { vertexBuffer_->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    // Draw lines
    vkCmdDraw(cmd, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}

} // namespace jupiter::rendering
