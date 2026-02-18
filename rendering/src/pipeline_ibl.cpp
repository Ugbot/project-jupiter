#include "rendering/pipeline_ibl.h"
#include "rendering/texture.h"
#include "assets/hdr_loader.h"
#include "assets/image_asset.h"
#include "logging/logging.h"
#include "profiling/profiler.h"

#include <fstream>
#include <cmath>
#include <array>

namespace jupiter {
namespace rendering {

// Helper to load SPIR-V shader
static std::vector<uint32_t> loadShaderSPIRV(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_ERROR("PipelineIBL", "Failed to open shader: %s", filepath.c_str());
        return {};
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize % sizeof(uint32_t) != 0) {
        LOG_ERROR("PipelineIBL", "Invalid SPIR-V file size: %s", filepath.c_str());
        return {};
    }
    
    std::vector<uint32_t> code(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);
    
    return code;
}

bool PipelineIBL::create(VkDevice device, VmaAllocator allocator,
                         VkCommandPool commandPool, VkQueue queue) {
    device_ = device;
    allocator_ = allocator;
    commandPool_ = commandPool;
    queue_ = queue;
    
    LOG_INFO("PipelineIBL", "Creating IBL pipeline...");
    
    if (!createCubemapRenderPass()) {
        LOG_ERROR("PipelineIBL", "Failed to create cubemap render pass");
        return false;
    }
    
    if (!createEquirectPipeline()) {
        LOG_ERROR("PipelineIBL", "Failed to create equirect-to-cube pipeline");
        return false;
    }
    
    if (!createFilterPipelines()) {
        LOG_ERROR("PipelineIBL", "Failed to create filter pipelines");
        return false;
    }
    
    LOG_INFO("PipelineIBL", "IBL pipeline created successfully");
    return true;
}

void PipelineIBL::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    
    vkDeviceWaitIdle(device_);
    
    if (equirectToCubePipeline_) vkDestroyPipeline(device_, equirectToCubePipeline_, nullptr);
    if (diffuseFilterPipeline_) vkDestroyPipeline(device_, diffuseFilterPipeline_, nullptr);
    if (specularFilterPipeline_) vkDestroyPipeline(device_, specularFilterPipeline_, nullptr);
    
    if (equirectLayout_) vkDestroyPipelineLayout(device_, equirectLayout_, nullptr);
    if (filterLayout_) vkDestroyPipelineLayout(device_, filterLayout_, nullptr);
    
    if (equirectDescLayout_) vkDestroyDescriptorSetLayout(device_, equirectDescLayout_, nullptr);
    if (filterDescLayout_) vkDestroyDescriptorSetLayout(device_, filterDescLayout_, nullptr);
    
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (cubemapRenderPass_) vkDestroyRenderPass(device_, cubemapRenderPass_, nullptr);
    
    hdrTexture_.reset();
    
    device_ = VK_NULL_HANDLE;
}

bool PipelineIBL::createCubemapRenderPass() {
    // Create render pass with 6 color attachments (one per cubemap face)
    // Matches HelloVulkan's CreateOffScreenCubemap exactly
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> attachmentRefs;
    
    constexpr VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    for (uint32_t face = 0; face < IBLConfig::LayerCount; ++face) {
        VkAttachmentDescription info = {
            .flags = 0,
            .format = IBLConfig::CubeFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            // HelloVulkan uses UNDEFINED - render pass handles the transition
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = finalLayout
        };
        
        VkAttachmentReference ref = {
            .attachment = face,
            .layout = finalLayout
        };
        
        attachments.push_back(info);
        attachmentRefs.push_back(ref);
    }
    
    VkSubpassDescription subpassDesc = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = static_cast<uint32_t>(attachmentRefs.size()),
        .pColorAttachments = attachmentRefs.data(),
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr
    };
    
    // HelloVulkan does NOT use subpass dependencies for cubemap render pass
    VkRenderPassCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpassDesc,
        .dependencyCount = 0,
        .pDependencies = nullptr
    };
    
    if (vkCreateRenderPass(device_, &createInfo, nullptr, &cubemapRenderPass_) != VK_SUCCESS) {
        return false;
    }
    
    LOG_INFO("PipelineIBL", "Created cubemap render pass with 6 attachments");
    return true;
}

bool PipelineIBL::createEquirectPipeline() {
    // Create descriptor set layout for equirect input texture
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &equirectDescLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &equirectDescLayout_,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };
    
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &equirectLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Load shaders
    auto vertCode = loadShaderSPIRV("shaders/ibl/fullscreen.vert.spv");
    auto fragCode = loadShaderSPIRV("shaders/ibl/equirect_to_cube.frag.spv");
    
    if (vertCode.empty() || fragCode.empty()) {
        return false;
    }
    
    VkShaderModule vertModule, fragModule;
    
    VkShaderModuleCreateInfo vertModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vertCode.size() * sizeof(uint32_t),
        .pCode = vertCode.data()
    };
    
    VkShaderModuleCreateInfo fragModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fragCode.size() * sizeof(uint32_t),
        .pCode = fragCode.data()
    };
    
    if (vkCreateShaderModule(device_, &vertModuleInfo, nullptr, &vertModule) != VK_SUCCESS ||
        vkCreateShaderModule(device_, &fragModuleInfo, nullptr, &fragModule) != VK_SUCCESS) {
        return false;
    }
    
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName = "main"
        }
    }};
    
    // Vertex input - empty (fullscreen triangle generated in shader)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0
    };
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };
    
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(IBLConfig::InputCubeSideLength),
        .height = static_cast<float>(IBLConfig::InputCubeSideLength),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {IBLConfig::InputCubeSideLength, IBLConfig::InputCubeSideLength}
    };
    
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };
    
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE
    };
    
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .stencilTestEnable = VK_FALSE
    };
    
    // 6 color blend attachments for MRT
    std::array<VkPipelineColorBlendAttachmentState, IBLConfig::LayerCount> blendAttachments{};
    for (auto& attachment : blendAttachments) {
        attachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = IBLConfig::LayerCount,
        .pAttachments = blendAttachments.data()
    };
    
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = nullptr,
        .layout = equirectLayout_,
        .renderPass = cubemapRenderPass_,
        .subpass = 0
    };
    
    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &equirectToCubePipeline_);
    
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, fragModule, nullptr);
    
    if (result != VK_SUCCESS) {
        return false;
    }
    
    LOG_INFO("PipelineIBL", "Created equirect-to-cube pipeline");
    return true;
}

bool PipelineIBL::createFilterPipelines() {
    // Create descriptor set layout for cubemap input
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &filterDescLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Create pipeline layout with push constants
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstCubeFilter)
    };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &filterDescLayout_,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &filterLayout_) != VK_SUCCESS) {
        return false;
    }
    
    // Load vertex shader (shared)
    auto vertCode = loadShaderSPIRV("shaders/ibl/fullscreen.vert.spv");
    if (vertCode.empty()) return false;
    
    VkShaderModule vertModule;
    VkShaderModuleCreateInfo vertModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vertCode.size() * sizeof(uint32_t),
        .pCode = vertCode.data()
    };
    
    if (vkCreateShaderModule(device_, &vertModuleInfo, nullptr, &vertModule) != VK_SUCCESS) {
        return false;
    }
    
    // Common pipeline configuration
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0
    };
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };
    
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE
    };
    
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE
    };
    
    std::array<VkPipelineColorBlendAttachmentState, IBLConfig::LayerCount> blendAttachments{};
    for (auto& attachment : blendAttachments) {
        attachment = {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = IBLConfig::LayerCount,
        .pAttachments = blendAttachments.data()
    };
    
    // Dynamic state for viewport/scissor (size varies per mip level)
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates.data()
    };
    
    VkViewport viewport = {0, 0, 128, 128, 0, 1};
    VkRect2D scissor = {{0, 0}, {128, 128}};
    
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };
    
    // Create diffuse filter pipeline
    auto diffuseFragCode = loadShaderSPIRV("shaders/ibl/irradiance_convolution.frag.spv");
    if (diffuseFragCode.empty()) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        return false;
    }
    
    VkShaderModule diffuseFragModule;
    VkShaderModuleCreateInfo diffuseFragModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = diffuseFragCode.size() * sizeof(uint32_t),
        .pCode = diffuseFragCode.data()
    };
    
    if (vkCreateShaderModule(device_, &diffuseFragModuleInfo, nullptr, &diffuseFragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        return false;
    }
    
    std::array<VkPipelineShaderStageCreateInfo, 2> diffuseStages = {{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = diffuseFragModule,
            .pName = "main"
        }
    }};
    
    VkGraphicsPipelineCreateInfo diffusePipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = diffuseStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = filterLayout_,
        .renderPass = cubemapRenderPass_,
        .subpass = 0
    };
    
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &diffusePipelineInfo, nullptr, &diffuseFilterPipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, diffuseFragModule, nullptr);
        return false;
    }
    
    vkDestroyShaderModule(device_, diffuseFragModule, nullptr);
    
    // Create specular filter pipeline
    auto specularFragCode = loadShaderSPIRV("shaders/ibl/prefilter_specular.frag.spv");
    if (specularFragCode.empty()) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        return false;
    }
    
    VkShaderModule specularFragModule;
    VkShaderModuleCreateInfo specularFragModuleInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = specularFragCode.size() * sizeof(uint32_t),
        .pCode = specularFragCode.data()
    };
    
    if (vkCreateShaderModule(device_, &specularFragModuleInfo, nullptr, &specularFragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        return false;
    }
    
    std::array<VkPipelineShaderStageCreateInfo, 2> specularStages = {{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = specularFragModule,
            .pName = "main"
        }
    }};
    
    VkGraphicsPipelineCreateInfo specularPipelineInfo = diffusePipelineInfo;
    specularPipelineInfo.pStages = specularStages.data();
    
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &specularPipelineInfo, nullptr, &specularFilterPipeline_) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vertModule, nullptr);
        vkDestroyShaderModule(device_, specularFragModule, nullptr);
        return false;
    }
    
    vkDestroyShaderModule(device_, vertModule, nullptr);
    vkDestroyShaderModule(device_, specularFragModule, nullptr);
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 10  // Enough for multiple operations
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 10,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        return false;
    }
    
    LOG_INFO("PipelineIBL", "Created filter pipelines");
    return true;
}

bool PipelineIBL::createCubemapImage(VulkanTexture* cubemap, uint32_t size, uint32_t mipLevels) {
    // Create cubemap image with 6 layers
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = IBLConfig::CubeFormat,
        .extent = {size, size, 1},
        .mipLevels = mipLevels,
        .arrayLayers = IBLConfig::LayerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY
    };
    
    VkImage image;
    VmaAllocation allocation;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    
    // Create cubemap image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format = IBLConfig::CubeFormat,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = IBLConfig::LayerCount
        }
    };
    
    VkImageView imageView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, image, allocation);
        return false;
    }
    
    // Set data in VulkanTexture
    cubemap->setImageData(device_, allocator_, image, imageView, allocation,
                         size, size, mipLevels, IBLConfig::LayerCount, IBLConfig::CubeFormat);
    
    // Create sampler
    if (!cubemap->createSampler(device_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                true, static_cast<float>(mipLevels))) {
        return false;
    }
    
    return true;
}

bool PipelineIBL::createCubemapFramebuffer(VulkanTexture* cubemap, uint32_t size, uint32_t mipLevel,
                                            VkFramebuffer& framebuffer, std::vector<VkImageView>& faceViews) {
    faceViews.resize(IBLConfig::LayerCount);
    
    VkImage image = cubemap->getImage();
    
    // Create view for each face at this mip level
    for (uint32_t face = 0; face < IBLConfig::LayerCount; ++face) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = IBLConfig::CubeFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = mipLevel,
                .levelCount = 1,
                .baseArrayLayer = face,
                .layerCount = 1
            }
        };
        
        if (vkCreateImageView(device_, &viewInfo, nullptr, &faceViews[face]) != VK_SUCCESS) {
            // Cleanup on failure
            for (uint32_t j = 0; j < face; ++j) {
                vkDestroyImageView(device_, faceViews[j], nullptr);
            }
            return false;
        }
    }
    
    // Create framebuffer
    VkFramebufferCreateInfo fbInfo = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = cubemapRenderPass_,
        .attachmentCount = IBLConfig::LayerCount,
        .pAttachments = faceViews.data(),
        .width = size,
        .height = size,
        .layers = 1
    };
    
    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        for (auto& view : faceViews) {
            vkDestroyImageView(device_, view, nullptr);
        }
        return false;
    }
    
    return true;
}

void PipelineIBL::transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkFormat format,
                                         VkImageLayout oldLayout, VkImageLayout newLayout,
                                         uint32_t baseMip, uint32_t mipCount,
                                         uint32_t baseLayer, uint32_t layerCount) {
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = baseMip,
            .levelCount = mipCount,
            .baseArrayLayer = baseLayer,
            .layerCount = layerCount
        }
    };
    
    VkPipelineStageFlags srcStage, dstStage;
    
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        // Generic fallback for any other transitions
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkCommandBuffer PipelineIBL::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

void PipelineIBL::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer
    };
    
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device_, &fenceInfo, nullptr, &fence);

    vkQueueSubmit(queue_, 1, &submitInfo, fence);
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

uint32_t PipelineIBL::calculateMipLevels(uint32_t size) {
    return static_cast<uint32_t>(std::floor(std::log2(size))) + 1;
}

bool PipelineIBL::convertEquirectToCubemap(const std::string& hdrFilePath,
                                            VulkanTexture* outputCubemap) {
    JUPITER_PROFILE_SCOPE("PipelineIBL::convertEquirectToCubemap");
    LOG_INFO("PipelineIBL", "Converting HDR to cubemap: %s", hdrFilePath.c_str());
    
    // Load HDR image
    assets::ImageAsset hdrAsset;
    if (!assets::HDRLoader::loadFromFile(hdrFilePath, hdrAsset, true)) {
        LOG_ERROR("PipelineIBL", "Failed to load HDR: %s", hdrFilePath.c_str());
        return false;
    }
    
    // Create HDR texture
    hdrTexture_ = std::make_unique<VulkanTexture>();
    if (!hdrTexture_->create(device_, allocator_, commandPool_, queue_,
                            hdrAsset.pixelData.data(), hdrAsset.width, hdrAsset.height,
                            VK_FORMAT_R32G32B32A32_SFLOAT, false)) {
        LOG_ERROR("PipelineIBL", "Failed to create HDR texture");
        return false;
    }
    
    if (!hdrTexture_->createSampler(device_, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false, 1.0f)) {
        LOG_ERROR("PipelineIBL", "Failed to create HDR sampler");
        return false;
    }
    
    // Create output cubemap
    uint32_t mipLevels = calculateMipLevels(IBLConfig::InputCubeSideLength);
    if (!createCubemapImage(outputCubemap, IBLConfig::InputCubeSideLength, mipLevels)) {
        LOG_ERROR("PipelineIBL", "Failed to create output cubemap");
        return false;
    }
    
    // Allocate descriptor set for HDR texture
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &equirectDescLayout_
    };
    
    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        LOG_ERROR("PipelineIBL", "Failed to allocate descriptor set");
        return false;
    }
    
    // Update descriptor set
    VkDescriptorImageInfo imageInfo = {
        .sampler = hdrTexture_->getSampler(),
        .imageView = hdrTexture_->getImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    
    // Create framebuffer
    VkFramebuffer framebuffer;
    std::vector<VkImageView> faceViews;
    if (!createCubemapFramebuffer(outputCubemap, IBLConfig::InputCubeSideLength, 0, framebuffer, faceViews)) {
        LOG_ERROR("PipelineIBL", "Failed to create framebuffer");
        return false;
    }
    
    // SPLIT INTO SEPARATE COMMAND BUFFER SUBMISSIONS (matches HelloVulkan pattern)
    // This avoids GPU timeout by submitting smaller work units

    // SUBMISSION 1: Initial transition only
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(cmd, outputCubemap->getImage(), IBLConfig::CubeFormat,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              0, mipLevels, 0, IBLConfig::LayerCount);
        endSingleTimeCommands(cmd);
    }

    // SUBMISSION 2: Render pass only
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();

        std::vector<VkClearValue> clearValues(IBLConfig::LayerCount, {{{0.0f, 0.0f, 1.0f, 1.0f}}});

        VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = cubemapRenderPass_,
            .framebuffer = framebuffer,
            .renderArea = {{0, 0}, {IBLConfig::InputCubeSideLength, IBLConfig::InputCubeSideLength}},
            .clearValueCount = static_cast<uint32_t>(clearValues.size()),
            .pClearValues = clearValues.data()
        };

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, equirectToCubePipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, equirectLayout_, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);  // Fullscreen triangle
        vkCmdEndRenderPass(cmd);

        endSingleTimeCommands(cmd);
    }

    // SUBMISSION 3: Mipmap generation (separate command buffer in generateCubemapMipmaps)
    generateCubemapMipmaps(outputCubemap, mipLevels);
    
    // Cleanup
    vkDestroyFramebuffer(device_, framebuffer, nullptr);
    for (auto& view : faceViews) {
        vkDestroyImageView(device_, view, nullptr);
    }
    
    LOG_INFO("PipelineIBL", "Cubemap created successfully (%ux%u, %u mips)",
             IBLConfig::InputCubeSideLength, IBLConfig::InputCubeSideLength, mipLevels);
    
    return true;
}

bool PipelineIBL::filterDiffuseIrradiance(VulkanTexture* inputCubemap,
                                           VulkanTexture* outputIrradiance) {
    JUPITER_PROFILE_SCOPE("PipelineIBL::filterDiffuseIrradiance");
    LOG_INFO("PipelineIBL", "Filtering diffuse irradiance...");
    
    // Create output irradiance cubemap (single mip level)
    if (!createCubemapImage(outputIrradiance, IBLConfig::OutputDiffuseSideLength, 1)) {
        return false;
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &filterDescLayout_
    };
    
    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return false;
    }
    
    // Update descriptor set with input cubemap
    VkDescriptorImageInfo imageInfo = {
        .sampler = inputCubemap->getSampler(),
        .imageView = inputCubemap->getImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    
    // Create framebuffer
    VkFramebuffer framebuffer;
    std::vector<VkImageView> faceViews;
    if (!createCubemapFramebuffer(outputIrradiance, IBLConfig::OutputDiffuseSideLength, 0, framebuffer, faceViews)) {
        return false;
    }
    
    // SPLIT INTO SEPARATE COMMAND BUFFER SUBMISSIONS (matches HelloVulkan pattern)
    // This avoids GPU timeout by submitting smaller work units
    // The irradiance convolution shader has ~15,800 samples per pixel - very heavy!

    // SUBMISSION 1: Initial transition only
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(cmd, outputIrradiance->getImage(), IBLConfig::CubeFormat,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              0, 1, 0, IBLConfig::LayerCount);
        endSingleTimeCommands(cmd);
    }

    // SUBMISSION 2: Render pass only (heavy convolution shader)
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();

        std::vector<VkClearValue> clearValues(IBLConfig::LayerCount, {{{0.0f, 0.0f, 1.0f, 1.0f}}});

        VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = cubemapRenderPass_,
            .framebuffer = framebuffer,
            .renderArea = {{0, 0}, {IBLConfig::OutputDiffuseSideLength, IBLConfig::OutputDiffuseSideLength}},
            .clearValueCount = static_cast<uint32_t>(clearValues.size()),
            .pClearValues = clearValues.data()
        };

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {0, 0, static_cast<float>(IBLConfig::OutputDiffuseSideLength),
                               static_cast<float>(IBLConfig::OutputDiffuseSideLength), 0, 1};
        VkRect2D scissor = {{0, 0}, {IBLConfig::OutputDiffuseSideLength, IBLConfig::OutputDiffuseSideLength}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, diffuseFilterPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, filterLayout_, 0, 1, &descriptorSet, 0, nullptr);

        PushConstCubeFilter pc = {0.0f, IBLConfig::OutputDiffuseSampleCount};
        vkCmdPushConstants(cmd, filterLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        endSingleTimeCommands(cmd);
    }

    // SUBMISSION 3: Final transition only
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(cmd, outputIrradiance->getImage(), IBLConfig::CubeFormat,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              0, 1, 0, IBLConfig::LayerCount);
        endSingleTimeCommands(cmd);
    }
    
    // Cleanup
    vkDestroyFramebuffer(device_, framebuffer, nullptr);
    for (auto& view : faceViews) {
        vkDestroyImageView(device_, view, nullptr);
    }
    
    LOG_INFO("PipelineIBL", "Diffuse irradiance filtered (%ux%u)",
             IBLConfig::OutputDiffuseSideLength, IBLConfig::OutputDiffuseSideLength);
    
    return true;
}

bool PipelineIBL::filterSpecularPrefiltered(VulkanTexture* inputCubemap,
                                             VulkanTexture* outputPrefiltered) {
    JUPITER_PROFILE_SCOPE("PipelineIBL::filterSpecularPrefiltered");
    LOG_INFO("PipelineIBL", "Filtering specular prefiltered...");
    
    uint32_t mipLevels = calculateMipLevels(IBLConfig::OutputSpecularSideLength);
    
    // Create output prefiltered cubemap with mipmaps
    if (!createCubemapImage(outputPrefiltered, IBLConfig::OutputSpecularSideLength, mipLevels)) {
        return false;
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &filterDescLayout_
    };
    
    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return false;
    }
    
    // Update descriptor set
    VkDescriptorImageInfo imageInfo = {
        .sampler = inputCubemap->getSampler(),
        .imageView = inputCubemap->getImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    
    // Pre-create all framebuffers and image views before recording commands
    // This is necessary because we need to clean them up AFTER command buffer execution
    struct MipResources {
        VkFramebuffer framebuffer;
        std::vector<VkImageView> faceViews;
        uint32_t size;
    };
    std::vector<MipResources> mipResources(mipLevels);
    
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipSize = IBLConfig::OutputSpecularSideLength >> mip;
        mipResources[mip].size = mipSize;
        if (!createCubemapFramebuffer(outputPrefiltered, mipSize, mip, 
                                       mipResources[mip].framebuffer, 
                                       mipResources[mip].faceViews)) {
            // Cleanup already created resources
            for (uint32_t j = 0; j < mip; ++j) {
                vkDestroyFramebuffer(device_, mipResources[j].framebuffer, nullptr);
                for (auto& view : mipResources[j].faceViews) {
                    vkDestroyImageView(device_, view, nullptr);
                }
            }
            return false;
        }
    }
    
    // SPLIT INTO SEPARATE COMMAND BUFFER SUBMISSIONS (matches HelloVulkan pattern)
    // Each mip level gets its own submission to avoid GPU timeout
    // The specular filtering shader has up to 1024 samples per pixel

    // Clear values for all 6 attachments (reused across submissions)
    std::vector<VkClearValue> clearValues(IBLConfig::LayerCount, {{{0.0f, 0.0f, 1.0f, 1.0f}}});

    // SUBMISSION 1: Initial transition for entire image
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(cmd, outputPrefiltered->getImage(), IBLConfig::CubeFormat,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              0, mipLevels, 0, IBLConfig::LayerCount);
        endSingleTimeCommands(cmd);
    }

    // RENDER EACH MIP LEVEL IN SEPARATE SUBMISSIONS
    // From highest mip to lowest (smallest to largest) for better GPU scheduling
    for (int mip = static_cast<int>(mipLevels) - 1; mip >= 0; --mip) {
        uint32_t mipSize = mipResources[mip].size;

        VkCommandBuffer cmd = beginSingleTimeCommands();

        // Bind pipeline and descriptor set for this submission
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, specularFilterPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, filterLayout_, 0, 1, &descriptorSet, 0, nullptr);

        VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = cubemapRenderPass_,
            .framebuffer = mipResources[mip].framebuffer,
            .renderArea = {{0, 0}, {mipSize, mipSize}},
            .clearValueCount = static_cast<uint32_t>(clearValues.size()),
            .pClearValues = clearValues.data()
        };

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {0, 0, static_cast<float>(mipSize), static_cast<float>(mipSize), 0, 1};
        VkRect2D scissor = {{0, 0}, {mipSize, mipSize}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        float roughness = mipLevels == 1 ? 0.0f : static_cast<float>(mip) / static_cast<float>(mipLevels - 1);
        PushConstCubeFilter pc = {roughness, IBLConfig::OutputDiffuseSampleCount};
        vkCmdPushConstants(cmd, filterLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        endSingleTimeCommands(cmd);
    }

    // FINAL SUBMISSION: Transition to shader read
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();
        transitionImageLayout(cmd, outputPrefiltered->getImage(), IBLConfig::CubeFormat,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              0, mipLevels, 0, IBLConfig::LayerCount);
        endSingleTimeCommands(cmd);
    }
    
    // Now safe to destroy framebuffers and image views
    for (auto& res : mipResources) {
        vkDestroyFramebuffer(device_, res.framebuffer, nullptr);
        for (auto& view : res.faceViews) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    
    LOG_INFO("PipelineIBL", "Specular prefiltered (%ux%u, %u mips)",
             IBLConfig::OutputSpecularSideLength, IBLConfig::OutputSpecularSideLength, mipLevels);

    return true;
}

void PipelineIBL::generateCubemapMipmaps(VulkanTexture* cubemap, uint32_t mipLevels) {
    JUPITER_PROFILE_SCOPE("PipelineIBL::generateCubemapMipmaps");
    if (mipLevels <= 1) {
        return; // No mipmaps to generate
    }

    VkCommandBuffer cmd = beginSingleTimeCommands();

    // Transition base level to transfer src
    transitionImageLayout(cmd, cubemap->getImage(), IBLConfig::CubeFormat,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          0, 1, 0, IBLConfig::LayerCount);

    // Blit from each mip level to the next
    for (uint32_t i = 1; i < mipLevels; ++i) {
        // Transition current mip level to transfer dst
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = cubemap->getImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = i,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = IBLConfig::LayerCount
            }
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Blit from previous mip to this mip (for all 6 faces)
        int32_t srcSize = static_cast<int32_t>(IBLConfig::InputCubeSideLength >> (i - 1));
        int32_t dstSize = static_cast<int32_t>(IBLConfig::InputCubeSideLength >> i);

        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = IBLConfig::LayerCount
            },
            .srcOffsets = {{0, 0, 0}, {srcSize, srcSize, 1}},
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = IBLConfig::LayerCount
            },
            .dstOffsets = {{0, 0, 0}, {dstSize, dstSize, 1}}
        };

        vkCmdBlitImage(cmd, cubemap->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      cubemap->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      1, &blit, VK_FILTER_LINEAR);

        // Transition this mip to transfer src for next iteration
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Final transition: all mip levels to shader read
    transitionImageLayout(cmd, cubemap->getImage(), IBLConfig::CubeFormat,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          0, mipLevels, 0, IBLConfig::LayerCount);

    endSingleTimeCommands(cmd);
}

} // namespace rendering
} // namespace jupiter

