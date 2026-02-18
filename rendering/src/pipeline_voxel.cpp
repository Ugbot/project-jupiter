/**
 * @file pipeline_voxel.cpp
 * @brief Voxel rendering pipeline implementation
 */

#include "rendering/pipeline_voxel.h"
#include "rendering/resources_shadow.h"
#include "logging/logging.h"
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <array>

namespace jupiter::rendering {

PipelineVoxel::PipelineVoxel(VkDevice device,
                             VkPhysicalDevice physicalDevice,
                             const PipelineConfig& config,
                             VkRenderPass renderPass,
                             VkFormat colorFormat,
                             VkFormat depthFormat)
    : PipelineBase(device, config)
    , physicalDevice_(physicalDevice)
    , externalRenderPass_(renderPass)
    , colorFormat_(colorFormat)
    , depthFormat_(depthFormat) {

    LOG_INFO("PipelineVoxel", "Creating voxel rendering pipeline");

    // Initialize chunk buffers
    chunkBuffers_.resize(MAX_CHUNKS);

    // Set default light
    currentLight_.sunDirection = glm::vec4(-0.5f, -0.5f, -0.7f, 1.0f);
    currentLight_.sunColor = glm::vec4(1.0f, 0.98f, 0.95f, 1.0f);
    currentLight_.ambientColor = glm::vec4(0.15f, 0.17f, 0.2f, 1.0f);

    createUBOBuffers();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSets();
    createPipeline();

    LOG_INFO("PipelineVoxel", "Voxel pipeline created successfully");
}

PipelineVoxel::~PipelineVoxel() {
    // Wait for device idle before cleanup
    vkDeviceWaitIdle(device_);

    // Cleanup shadow resources
    if (shadowPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
    }
    if (shadowDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, shadowDescriptorPool_, nullptr);
    }
    if (shadowDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, shadowDescriptorSetLayout_, nullptr);
    }

    // Cleanup depth pass resources
    if (depthPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, depthPipeline_, nullptr);
    }
    if (depthPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, depthPipelineLayout_, nullptr);
    }
    if (depthDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, depthDescriptorPool_, nullptr);
    }
    if (depthDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, depthDescriptorSetLayout_, nullptr);
    }

    // Cleanup pending deletions (device is idle, so safe to destroy immediately)
    for (auto& deletion : pendingDeletions_) {
        if (deletion.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, deletion.vertexBuffer, nullptr);
        }
        if (deletion.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, deletion.vertexMemory, nullptr);
        }
        if (deletion.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, deletion.indexBuffer, nullptr);
        }
        if (deletion.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, deletion.indexMemory, nullptr);
        }
    }
    pendingDeletions_.clear();

    // Cleanup chunk buffers
    for (auto& chunk : chunkBuffers_) {
        if (chunk.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, chunk.vertexBuffer, nullptr);
        }
        if (chunk.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, chunk.vertexMemory, nullptr);
        }
        if (chunk.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, chunk.indexBuffer, nullptr);
        }
        if (chunk.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, chunk.indexMemory, nullptr);
        }
    }

    // Cleanup UBOs
    for (auto& ubo : cameraUBOs_) {
        if (ubo.mappedData) {
            vkUnmapMemory(device_, ubo.memory);
        }
        if (ubo.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, ubo.buffer, nullptr);
        }
        if (ubo.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, ubo.memory, nullptr);
        }
    }

    for (auto& ubo : lightUBOs_) {
        if (ubo.mappedData) {
            vkUnmapMemory(device_, ubo.memory);
        }
        if (ubo.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, ubo.buffer, nullptr);
        }
        if (ubo.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, ubo.memory, nullptr);
        }
    }
}

uint32_t PipelineVoxel::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

void PipelineVoxel::createUBOBuffers() {
    constexpr uint32_t FRAMES_IN_FLIGHT = 2;
    cameraUBOs_.resize(FRAMES_IN_FLIGHT);
    lightUBOs_.resize(FRAMES_IN_FLIGHT);

    VkDeviceSize cameraSize = sizeof(CameraUBO);
    VkDeviceSize lightSize = sizeof(VoxelLightUBO);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        // Camera UBO
        {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = cameraSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device_, &bufferInfo, nullptr, &cameraUBOs_[i].buffer) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create voxel camera UBO");
            }

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(device_, cameraUBOs_[i].buffer, &memReqs);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (vkAllocateMemory(device_, &allocInfo, nullptr, &cameraUBOs_[i].memory) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate voxel camera UBO memory");
            }

            vkBindBufferMemory(device_, cameraUBOs_[i].buffer, cameraUBOs_[i].memory, 0);
            vkMapMemory(device_, cameraUBOs_[i].memory, 0, cameraSize, 0, &cameraUBOs_[i].mappedData);
            cameraUBOs_[i].size = cameraSize;
        }

        // Light UBO
        {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = lightSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device_, &bufferInfo, nullptr, &lightUBOs_[i].buffer) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create voxel light UBO");
            }

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(device_, lightUBOs_[i].buffer, &memReqs);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            if (vkAllocateMemory(device_, &allocInfo, nullptr, &lightUBOs_[i].memory) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate voxel light UBO memory");
            }

            vkBindBufferMemory(device_, lightUBOs_[i].buffer, lightUBOs_[i].memory, 0);
            vkMapMemory(device_, lightUBOs_[i].memory, 0, lightSize, 0, &lightUBOs_[i].mappedData);
            lightUBOs_[i].size = lightSize;

            // Initialize with default light
            std::memcpy(lightUBOs_[i].mappedData, &currentLight_, sizeof(VoxelLightUBO));
        }
    }
}

void PipelineVoxel::createDescriptorSetLayout() {
    // Binding 0: Camera UBO
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: Light UBO
    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 1;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {cameraBinding, lightBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel descriptor set layout");
    }
}

void PipelineVoxel::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 4;  // 2 camera + 2 light (for 2 frames)

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;  // One per frame in flight

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel descriptor pool");
    }
}

void PipelineVoxel::createDescriptorSets() {
    voxelDescriptorSets_.resize(2);

    std::vector<VkDescriptorSetLayout> layouts(2, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &allocInfo, voxelDescriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate voxel descriptor sets");
    }

    for (uint32_t i = 0; i < 2; i++) {
        VkDescriptorBufferInfo cameraInfo = cameraUBOs_[i].getDescriptorInfo();
        VkDescriptorBufferInfo lightInfo = lightUBOs_[i].getDescriptorInfo();

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        // Camera UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = voxelDescriptorSets_[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &cameraInfo;

        // Light UBO
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = voxelDescriptorSets_[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &lightInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}

void PipelineVoxel::createPipeline() {
    VkShaderModule vertShaderModule = loadShaderModule("shaders/voxel/voxel_simple.vert.spv");
    VkShaderModule fragShaderModule = loadShaderModule("shaders/voxel/voxel_simple.frag.spv");

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

    // Vertex input - VoxelVertexGPU (8 bytes)
    auto vertexDesc = VoxelVertexGPU::getDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDesc.bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexDesc.bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDesc.attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexDesc.attributes.data();

    // Input assembly - stb_voxel_render outputs quads as triangle lists
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

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Disabled for debugging
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

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
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VoxelPushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel pipeline layout");
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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = externalRenderPass_;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel graphics pipeline");
    }

    vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragShaderModule, nullptr);

    LOG_DEBUG("PipelineVoxel", "Voxel pipeline created");
}

void PipelineVoxel::onWindowResized(uint32_t width, uint32_t height) {
    // Update config for viewport calculations
    config_.viewportWidth = width;
    config_.viewportHeight = height;
}

void PipelineVoxel::setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) {
    currentCamera_ = ubo;
    std::memcpy(cameraUBOs_[frameIndex].mappedData, &ubo, sizeof(CameraUBO));
}

void PipelineVoxel::setLightUBO(const VoxelLightUBO& light) {
    currentLight_ = light;
    // Update all frames
    for (auto& ubo : lightUBOs_) {
        std::memcpy(ubo.mappedData, &light, sizeof(VoxelLightUBO));
    }
}

bool PipelineVoxel::uploadChunkMesh(uint32_t chunkIndex,
                                    const void* vertices,
                                    size_t numBytes,
                                    const glm::vec3& worldOffset,
                                    const glm::vec3& scale) {
    if (chunkIndex >= MAX_CHUNKS || !vertices || numBytes == 0) {
        return false;
    }

    auto& chunk = chunkBuffers_[chunkIndex];

    // Queue existing buffers for deferred deletion (GPU may still be using them)
    // Don't destroy immediately - wait until all in-flight frames are done
    if (chunk.vertexBuffer != VK_NULL_HANDLE || chunk.indexBuffer != VK_NULL_HANDLE) {
        PendingDeletion deletion;
        deletion.vertexBuffer = chunk.vertexBuffer;
        deletion.vertexMemory = chunk.vertexMemory;
        deletion.indexBuffer = chunk.indexBuffer;
        deletion.indexMemory = chunk.indexMemory;
        deletion.frameQueued = globalFrameCounter_;
        pendingDeletions_.push_back(deletion);

        // Clear the chunk's references (buffers now owned by deletion queue)
        chunk.vertexBuffer = VK_NULL_HANDLE;
        chunk.vertexMemory = VK_NULL_HANDLE;
        chunk.indexBuffer = VK_NULL_HANDLE;
        chunk.indexMemory = VK_NULL_HANDLE;
    }

    // Calculate vertex and index counts
    // stb_voxel_render outputs quads (4 vertices each)
    uint32_t vertexCount = static_cast<uint32_t>(numBytes / sizeof(VoxelVertexGPU));
    if (vertexCount % 4 != 0) {
        LOG_WARN("PipelineVoxel", "Vertex count %u not multiple of 4 (quads)", vertexCount);
    }
    uint32_t numQuads = vertexCount / 4;
    uint32_t indexCount = numQuads * 6;  // 2 triangles per quad, 3 indices each

    // Create vertex buffer with host-visible memory
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = numBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &chunk.vertexBuffer) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create vertex buffer");
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device_, chunk.vertexBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &chunk.vertexMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, chunk.vertexBuffer, nullptr);
        chunk.vertexBuffer = VK_NULL_HANDLE;
        LOG_ERROR("PipelineVoxel", "Failed to allocate vertex memory");
        return false;
    }

    vkBindBufferMemory(device_, chunk.vertexBuffer, chunk.vertexMemory, 0);

    // Upload vertex data
    void* vertData;
    vkMapMemory(device_, chunk.vertexMemory, 0, numBytes, 0, &vertData);
    std::memcpy(vertData, vertices, numBytes);
    vkUnmapMemory(device_, chunk.vertexMemory);

    // Create index buffer
    VkDeviceSize indexBufferSize = indexCount * sizeof(uint32_t);

    bufferInfo.size = indexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &chunk.indexBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(device_, chunk.vertexBuffer, nullptr);
        vkFreeMemory(device_, chunk.vertexMemory, nullptr);
        chunk.vertexBuffer = VK_NULL_HANDLE;
        chunk.vertexMemory = VK_NULL_HANDLE;
        LOG_ERROR("PipelineVoxel", "Failed to create index buffer");
        return false;
    }

    vkGetBufferMemoryRequirements(device_, chunk.indexBuffer, &memReqs);
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &chunk.indexMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, chunk.vertexBuffer, nullptr);
        vkFreeMemory(device_, chunk.vertexMemory, nullptr);
        vkDestroyBuffer(device_, chunk.indexBuffer, nullptr);
        chunk.vertexBuffer = VK_NULL_HANDLE;
        chunk.vertexMemory = VK_NULL_HANDLE;
        chunk.indexBuffer = VK_NULL_HANDLE;
        LOG_ERROR("PipelineVoxel", "Failed to allocate index memory");
        return false;
    }

    vkBindBufferMemory(device_, chunk.indexBuffer, chunk.indexMemory, 0);

    // Generate and upload index data (quad to triangle conversion)
    uint32_t* indexData;
    vkMapMemory(device_, chunk.indexMemory, 0, indexBufferSize, 0, (void**)&indexData);

    // Each quad (4 vertices) becomes 2 triangles (6 indices)
    // Quad vertex order from stb_voxel_render: 0-1-2-3 (CCW when viewed from front)
    // Triangle 1: 0-1-2
    // Triangle 2: 0-2-3
    for (uint32_t q = 0; q < numQuads; ++q) {
        uint32_t base = q * 4;
        indexData[q * 6 + 0] = base + 0;
        indexData[q * 6 + 1] = base + 1;
        indexData[q * 6 + 2] = base + 2;
        indexData[q * 6 + 3] = base + 0;
        indexData[q * 6 + 4] = base + 2;
        indexData[q * 6 + 5] = base + 3;
    }

    vkUnmapMemory(device_, chunk.indexMemory);

    // Set chunk metadata
    chunk.vertexCount = vertexCount;
    chunk.indexCount = indexCount;
    chunk.worldOffset = worldOffset;
    chunk.scale = scale;
    chunk.valid = true;
    return true;
}

void PipelineVoxel::clearChunkMesh(uint32_t chunkIndex) {
    if (chunkIndex >= MAX_CHUNKS) return;

    auto& chunk = chunkBuffers_[chunkIndex];

    // Queue existing buffers for deferred deletion (GPU may still be using them)
    if (chunk.vertexBuffer != VK_NULL_HANDLE || chunk.indexBuffer != VK_NULL_HANDLE) {
        PendingDeletion deletion;
        deletion.vertexBuffer = chunk.vertexBuffer;
        deletion.vertexMemory = chunk.vertexMemory;
        deletion.indexBuffer = chunk.indexBuffer;
        deletion.indexMemory = chunk.indexMemory;
        deletion.frameQueued = globalFrameCounter_;
        pendingDeletions_.push_back(deletion);
    }

    chunk.vertexBuffer = VK_NULL_HANDLE;
    chunk.vertexMemory = VK_NULL_HANDLE;
    chunk.indexBuffer = VK_NULL_HANDLE;
    chunk.indexMemory = VK_NULL_HANDLE;
    chunk.vertexCount = 0;
    chunk.indexCount = 0;
    chunk.valid = false;
}

uint32_t PipelineVoxel::getActiveChunkCount() const {
    uint32_t count = 0;
    for (const auto& chunk : chunkBuffers_) {
        if (chunk.valid) count++;
    }
    return count;
}

void PipelineVoxel::fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Track current frame for deferred deletion
    currentFrameIndex_ = frameIndex;
    globalFrameCounter_++;
    processDeletionQueue();

    // Bind appropriate pipeline based on shadow state
    VkPipeline activePipeline = shadowEnabled_ && shadowPipeline_ != VK_NULL_HANDLE ? shadowPipeline_ : pipeline_;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(config_.viewportWidth);
    viewport.height = static_cast<float>(config_.viewportHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {config_.viewportWidth, config_.viewportHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind appropriate descriptor set based on shadow state
    VkDescriptorSet* activeDescriptorSet = shadowEnabled_ && !shadowDescriptorSets_.empty()
        ? &shadowDescriptorSets_[frameIndex]
        : &voxelDescriptorSets_[frameIndex];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                           0, 1, activeDescriptorSet, 0, nullptr);

    uint32_t renderCount = 0;

    // Frustum planes from viewProjection matrix
    glm::mat4 vp = currentCamera_.viewProjection;
    glm::vec4 frustumPlanes[6];
    // Left
    frustumPlanes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    // Right
    frustumPlanes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    // Bottom
    frustumPlanes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    // Top
    frustumPlanes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    // Near
    frustumPlanes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    // Far
    frustumPlanes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(frustumPlanes[i]));
        frustumPlanes[i] /= len;
    }

    uint32_t culledCount = 0;

    // Draw each valid chunk
    for (const auto& chunk : chunkBuffers_) {
        if (!chunk.valid || chunk.vertexBuffer == VK_NULL_HANDLE || chunk.indexBuffer == VK_NULL_HANDLE) continue;

        // Calculate chunk AABB center and half-extents
        constexpr float CHUNK_SIZE = 16.0f;
        glm::vec3 halfExtent = glm::vec3(CHUNK_SIZE * 0.5f) * chunk.scale;
        glm::vec3 center = chunk.worldOffset + halfExtent;

        // TEMPORARILY DISABLED: All frustum and underground culling disabled for debugging
        // TODO: Re-enable with proper plane selection (skip bottom/near)
        bool visible = true;  // Always render - let GPU clip
        (void)frustumPlanes;  // Suppress unused warning
        (void)center;
        (void)halfExtent;

        if (!visible) {
            culledCount++;
            continue;
        }

        renderCount++;

        // Set push constants
        VoxelPushConstants pushConstants;
        pushConstants.chunkOffset = glm::vec4(chunk.worldOffset, 0.0f);
        pushConstants.scale = glm::vec4(chunk.scale, 0.0f);

        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(VoxelPushConstants), &pushConstants);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {chunk.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        // Bind index buffer
        vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Draw indexed (quads converted to triangles)
        vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);
    }
    
    (void)renderCount;
    (void)culledCount;
}

void PipelineVoxel::enableShadowMapping(ResourcesShadow* shadowResources) {
    if (!shadowResources || !shadowResources->isValid()) {
        LOG_ERROR("PipelineVoxel", "Invalid shadow resources provided");
        return;
    }

    shadowResources_ = shadowResources;
    LOG_INFO("PipelineVoxel", "Enabling shadow mapping...");

    // Create shadow descriptor set layout with 4 bindings:
    // 0: Camera UBO
    // 1: Light UBO
    // 2: Shadow UBO
    // 3: Shadow map sampler
    std::array<VkDescriptorSetLayoutBinding, 4> shadowBindings{};

    // Binding 0: Camera UBO
    shadowBindings[0].binding = 0;
    shadowBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowBindings[0].descriptorCount = 1;
    shadowBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: Light UBO
    shadowBindings[1].binding = 1;
    shadowBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowBindings[1].descriptorCount = 1;
    shadowBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 2: Shadow UBO
    shadowBindings[2].binding = 2;
    shadowBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowBindings[2].descriptorCount = 1;
    shadowBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 3: Shadow map (comparison sampler)
    shadowBindings[3].binding = 3;
    shadowBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBindings[3].descriptorCount = 1;
    shadowBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(shadowBindings.size());
    layoutInfo.pBindings = shadowBindings.data();

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &shadowDescriptorSetLayout_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create shadow descriptor set layout");
        return;
    }

    // Create shadow descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 6;  // 2 camera + 2 light + 2 shadow (for 2 frames)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2;  // 1 shadow map per frame

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;  // One per frame in flight

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &shadowDescriptorPool_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create shadow descriptor pool");
        return;
    }

    // Allocate shadow descriptor sets
    shadowDescriptorSets_.resize(2);
    std::vector<VkDescriptorSetLayout> layouts(2, shadowDescriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = shadowDescriptorPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device_, &allocInfo, shadowDescriptorSets_.data()) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to allocate shadow descriptor sets");
        return;
    }

    // Update shadow descriptor sets
    for (uint32_t i = 0; i < 2; i++) {
        VkDescriptorBufferInfo cameraInfo = cameraUBOs_[i].getDescriptorInfo();
        VkDescriptorBufferInfo lightInfo = lightUBOs_[i].getDescriptorInfo();
        VkDescriptorBufferInfo shadowInfo = shadowResources_->getUBO(i).getDescriptorInfo();
        VkDescriptorImageInfo shadowMapInfo = shadowResources_->getShadowMapDescriptor();

        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};

        // Camera UBO
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = shadowDescriptorSets_[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &cameraInfo;

        // Light UBO
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = shadowDescriptorSets_[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &lightInfo;

        // Shadow UBO
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = shadowDescriptorSets_[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &shadowInfo;

        // Shadow map
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = shadowDescriptorSets_[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &shadowMapInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    // Create shadow-enabled pipeline with voxel_shadow.frag shader
    VkShaderModule vertShaderModule = loadShaderModule("shaders/voxel/voxel_simple.vert.spv");
    VkShaderModule fragShaderModule = loadShaderModule("shaders/voxel/voxel_shadow.frag.spv");

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

    // Vertex input - VoxelVertexGPU (8 bytes)
    auto vertexDesc = VoxelVertexGPU::getDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexDesc.bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexDesc.bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexDesc.attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexDesc.attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
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

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(VoxelPushConstants);

    // Create new pipeline layout with shadow descriptor set layout
    VkPipelineLayout shadowPipelineLayout;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shadowDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create shadow pipeline layout");
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        return;
    }

    // Create shadow pipeline
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
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = externalRenderPass_;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create shadow graphics pipeline");
        vkDestroyPipelineLayout(device_, shadowPipelineLayout, nullptr);
        vkDestroyShaderModule(device_, vertShaderModule, nullptr);
        vkDestroyShaderModule(device_, fragShaderModule, nullptr);
        return;
    }

    // Replace old pipeline layout with shadow layout
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    pipelineLayout_ = shadowPipelineLayout;

    vkDestroyShaderModule(device_, vertShaderModule, nullptr);
    vkDestroyShaderModule(device_, fragShaderModule, nullptr);

    shadowEnabled_ = true;
    LOG_INFO("PipelineVoxel", "Shadow mapping enabled successfully");

    // ==========================================================================
    // Create depth pass pipeline (renders to shadow map from light's view)
    // ==========================================================================
    LOG_INFO("PipelineVoxel", "Creating voxel depth pass pipeline...");

    // Depth descriptor set layout - only needs Shadow UBO (binding 0)
    VkDescriptorSetLayoutBinding depthUboBinding{};
    depthUboBinding.binding = 0;
    depthUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    depthUboBinding.descriptorCount = 1;
    depthUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo depthLayoutInfo{};
    depthLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    depthLayoutInfo.bindingCount = 1;
    depthLayoutInfo.pBindings = &depthUboBinding;

    if (vkCreateDescriptorSetLayout(device_, &depthLayoutInfo, nullptr, &depthDescriptorSetLayout_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create depth descriptor set layout");
        return;
    }

    // Depth descriptor pool
    VkDescriptorPoolSize depthPoolSize{};
    depthPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    depthPoolSize.descriptorCount = 2;  // One per frame in flight

    VkDescriptorPoolCreateInfo depthPoolInfo{};
    depthPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    depthPoolInfo.poolSizeCount = 1;
    depthPoolInfo.pPoolSizes = &depthPoolSize;
    depthPoolInfo.maxSets = 2;

    if (vkCreateDescriptorPool(device_, &depthPoolInfo, nullptr, &depthDescriptorPool_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create depth descriptor pool");
        return;
    }

    // Allocate depth descriptor sets
    depthDescriptorSets_.resize(2);
    std::vector<VkDescriptorSetLayout> depthLayouts(2, depthDescriptorSetLayout_);

    VkDescriptorSetAllocateInfo depthAllocInfo{};
    depthAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    depthAllocInfo.descriptorPool = depthDescriptorPool_;
    depthAllocInfo.descriptorSetCount = 2;
    depthAllocInfo.pSetLayouts = depthLayouts.data();

    if (vkAllocateDescriptorSets(device_, &depthAllocInfo, depthDescriptorSets_.data()) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to allocate depth descriptor sets");
        return;
    }

    // Update depth descriptor sets with Shadow UBO
    for (uint32_t i = 0; i < 2; i++) {
        VkDescriptorBufferInfo depthShadowInfo = shadowResources_->getUBO(i).getDescriptorInfo();

        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = depthDescriptorSets_[i];
        depthWrite.dstBinding = 0;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        depthWrite.descriptorCount = 1;
        depthWrite.pBufferInfo = &depthShadowInfo;

        vkUpdateDescriptorSets(device_, 1, &depthWrite, 0, nullptr);
    }

    // Load depth shaders
    VkShaderModule depthVertShader = loadShaderModule("shaders/shadow/depth_voxel.vert.spv");
    VkShaderModule depthFragShader = loadShaderModule("shaders/shadow/depth.frag.spv");

    VkPipelineShaderStageCreateInfo depthVertStageInfo{};
    depthVertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    depthVertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    depthVertStageInfo.module = depthVertShader;
    depthVertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo depthFragStageInfo{};
    depthFragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    depthFragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    depthFragStageInfo.module = depthFragShader;
    depthFragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo depthStages[] = {depthVertStageInfo, depthFragStageInfo};

    // Vertex input - VoxelVertexGPU format
    auto depthVertexDesc = VoxelVertexGPU::getDescription();

    VkPipelineVertexInputStateCreateInfo depthVertexInputInfo{};
    depthVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    depthVertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(depthVertexDesc.bindings.size());
    depthVertexInputInfo.pVertexBindingDescriptions = depthVertexDesc.bindings.data();
    depthVertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(depthVertexDesc.attributes.size());
    depthVertexInputInfo.pVertexAttributeDescriptions = depthVertexDesc.attributes.data();

    VkPipelineInputAssemblyStateCreateInfo depthInputAssembly{};
    depthInputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    depthInputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    depthInputAssembly.primitiveRestartEnable = VK_FALSE;

    // Fixed viewport/scissor for shadow map resolution
    uint32_t shadowRes = shadowResources_->getResolution();
    VkViewport depthViewport{};
    depthViewport.x = 0.0f;
    depthViewport.y = 0.0f;
    depthViewport.width = static_cast<float>(shadowRes);
    depthViewport.height = static_cast<float>(shadowRes);
    depthViewport.minDepth = 0.0f;
    depthViewport.maxDepth = 1.0f;

    VkRect2D depthScissor{};
    depthScissor.offset = {0, 0};
    depthScissor.extent = {shadowRes, shadowRes};

    VkPipelineViewportStateCreateInfo depthViewportState{};
    depthViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    depthViewportState.viewportCount = 1;
    depthViewportState.pViewports = &depthViewport;
    depthViewportState.scissorCount = 1;
    depthViewportState.pScissors = &depthScissor;

    // Rasterization - depth bias for shadow acne
    VkPipelineRasterizationStateCreateInfo depthRasterizer{};
    depthRasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    depthRasterizer.depthClampEnable = VK_FALSE;
    depthRasterizer.rasterizerDiscardEnable = VK_FALSE;
    depthRasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    depthRasterizer.lineWidth = 1.0f;
    depthRasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for single-sided voxel geometry
    depthRasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    depthRasterizer.depthBiasEnable = VK_TRUE;
    depthRasterizer.depthBiasConstantFactor = 1.25f;
    depthRasterizer.depthBiasClamp = 0.0f;
    depthRasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo depthMultisampling{};
    depthMultisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    depthMultisampling.sampleShadingEnable = VK_FALSE;
    depthMultisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.depthTestEnable = VK_TRUE;
    depthStencilState.depthWriteEnable = VK_TRUE;
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.stencilTestEnable = VK_FALSE;

    // No color blending for depth-only pass
    VkPipelineColorBlendStateCreateInfo depthColorBlending{};
    depthColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    depthColorBlending.attachmentCount = 0;

    // Push constants for voxel chunks (same layout as VoxelPushConstants)
    VkPushConstantRange depthPushConstantRange{};
    depthPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    depthPushConstantRange.offset = 0;
    depthPushConstantRange.size = sizeof(VoxelPushConstants);

    // Depth pipeline layout
    VkPipelineLayoutCreateInfo depthPipelineLayoutInfo{};
    depthPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    depthPipelineLayoutInfo.setLayoutCount = 1;
    depthPipelineLayoutInfo.pSetLayouts = &depthDescriptorSetLayout_;
    depthPipelineLayoutInfo.pushConstantRangeCount = 1;
    depthPipelineLayoutInfo.pPushConstantRanges = &depthPushConstantRange;

    if (vkCreatePipelineLayout(device_, &depthPipelineLayoutInfo, nullptr, &depthPipelineLayout_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create depth pipeline layout");
        vkDestroyShaderModule(device_, depthVertShader, nullptr);
        vkDestroyShaderModule(device_, depthFragShader, nullptr);
        return;
    }

    // Create depth pipeline
    VkGraphicsPipelineCreateInfo depthPipelineInfo{};
    depthPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    depthPipelineInfo.stageCount = 2;
    depthPipelineInfo.pStages = depthStages;
    depthPipelineInfo.pVertexInputState = &depthVertexInputInfo;
    depthPipelineInfo.pInputAssemblyState = &depthInputAssembly;
    depthPipelineInfo.pViewportState = &depthViewportState;
    depthPipelineInfo.pRasterizationState = &depthRasterizer;
    depthPipelineInfo.pMultisampleState = &depthMultisampling;
    depthPipelineInfo.pDepthStencilState = &depthStencilState;
    depthPipelineInfo.pColorBlendState = &depthColorBlending;
    depthPipelineInfo.layout = depthPipelineLayout_;
    depthPipelineInfo.renderPass = shadowResources_->getRenderPass();
    depthPipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &depthPipelineInfo, nullptr, &depthPipeline_) != VK_SUCCESS) {
        LOG_ERROR("PipelineVoxel", "Failed to create voxel depth pipeline");
        vkDestroyPipelineLayout(device_, depthPipelineLayout_, nullptr);
        depthPipelineLayout_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, depthVertShader, nullptr);
        vkDestroyShaderModule(device_, depthFragShader, nullptr);
        return;
    }

    vkDestroyShaderModule(device_, depthVertShader, nullptr);
    vkDestroyShaderModule(device_, depthFragShader, nullptr);

    LOG_INFO("PipelineVoxel", "Voxel depth pass pipeline created successfully");
}

void PipelineVoxel::updateShadowUBO(uint32_t frameIndex) {
    // Shadow UBO is updated by ResourcesShadow, nothing to do here
    // This method exists for future extensions (e.g., per-frame shadow params)
    (void)frameIndex;
}

void PipelineVoxel::fillShadowDepthBuffer(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Only render if shadow mapping is enabled and depth pipeline is valid
    if (!shadowEnabled_ || !shadowResources_ || depthPipeline_ == VK_NULL_HANDLE) {
        return;
    }

    // Begin shadow render pass
    VkClearValue clearValue{};
    clearValue.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = shadowResources_->getRenderPass();
    renderPassInfo.framebuffer = shadowResources_->getFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {shadowResources_->getResolution(), shadowResources_->getResolution()};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind depth pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPipeline_);

    // NOTE: Viewport and scissor are baked into the pipeline (non-dynamic)
    // so we don't need to set them here

    // Bind depth descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPipelineLayout_,
                           0, 1, &depthDescriptorSets_[frameIndex], 0, nullptr);

    // Draw each valid chunk
    uint32_t renderCount = 0;
    for (const auto& chunk : chunkBuffers_) {
        if (!chunk.valid || chunk.vertexBuffer == VK_NULL_HANDLE || chunk.indexBuffer == VK_NULL_HANDLE) {
            continue;
        }

        // Set push constants (chunk offset + scale)
        VoxelPushConstants pushConstants;
        pushConstants.chunkOffset = glm::vec4(chunk.worldOffset, 0.0f);
        pushConstants.scale = glm::vec4(chunk.scale, 0.0f);

        vkCmdPushConstants(cmd, depthPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(VoxelPushConstants), &pushConstants);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = {chunk.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        // Bind index buffer
        vkCmdBindIndexBuffer(cmd, chunk.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Draw indexed
        vkCmdDrawIndexed(cmd, chunk.indexCount, 1, 0, 0, 0);

        renderCount++;
    }

    vkCmdEndRenderPass(cmd);

    (void)renderCount;  // Avoid unused warning
}

void PipelineVoxel::processDeletionQueue() {
    // Process pending deletions - delete buffers that are old enough that
    // no in-flight frames could possibly still be using them
    auto it = pendingDeletions_.begin();
    while (it != pendingDeletions_.end()) {
        // Calculate frame age using global monotonic counter
        // With 2 frames in flight, we need to wait at least 2 frames before deleting
        uint64_t frameAge = globalFrameCounter_ - it->frameQueued;

        // Safe to delete after FRAMES_IN_FLIGHT frames have completed
        if (frameAge >= FRAMES_IN_FLIGHT) {
            // Actually destroy the Vulkan resources now
            if (it->vertexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, it->vertexBuffer, nullptr);
            }
            if (it->vertexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, it->vertexMemory, nullptr);
            }
            if (it->indexBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, it->indexBuffer, nullptr);
            }
            if (it->indexMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, it->indexMemory, nullptr);
            }

            it = pendingDeletions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace jupiter::rendering
