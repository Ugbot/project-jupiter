#include "rendering/vulkan_compute_pipeline.h"
#include "logging/logging.h"
#include <fstream>
#include <vector>

namespace jupiter {
namespace rendering {
namespace vulkan {

bool VulkanComputePipeline::create(VkDevice device,
                                    const std::string& shaderPath,
                                    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
                                    const VkPushConstantRange* pushConstantRange) {
    LOG_INFO("ComputePipeline", "Creating compute pipeline from shader: %s", shaderPath.c_str());

    if (device == VK_NULL_HANDLE) {
        LOG_ERROR("ComputePipeline", "Invalid device handle");
        return false;
    }

    destroy(); // Clean up any existing pipeline

    device_ = device;

    // Load shader
    if (!loadShader(shaderPath)) {
        LOG_ERROR("ComputePipeline", "Failed to load shader: %s", shaderPath.c_str());
        return false;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    layoutInfo.pSetLayouts = descriptorSetLayouts.empty() ? nullptr : descriptorSetLayouts.data();
    layoutInfo.pushConstantRangeCount = pushConstantRange ? 1 : 0;
    layoutInfo.pPushConstantRanges = pushConstantRange;

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create pipeline layout");
        return false;
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule_;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout_;

    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create compute pipeline");
        return false;
    }

    LOG_INFO("ComputePipeline", "Compute pipeline created successfully");
    return true;
}

void VulkanComputePipeline::destroy() {
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

    device_ = VK_NULL_HANDLE;
}

void VulkanComputePipeline::dispatch(VkCommandBuffer commandBuffer,
                                     uint32_t groupCountX,
                                     uint32_t groupCountY,
                                     uint32_t groupCountZ) const {
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanComputePipeline::barrier(VkCommandBuffer commandBuffer,
                                    VkPipelineStageFlags srcStage,
                                    VkPipelineStageFlags dstStage,
                                    VkAccessFlags srcAccess,
                                    VkAccessFlags dstAccess) {
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );
}

void VulkanComputePipeline::imageBarrier(VkCommandBuffer commandBuffer,
                                         VkImage image,
                                         VkImageLayout oldLayout,
                                         VkImageLayout newLayout,
                                         VkPipelineStageFlags srcStage,
                                         VkPipelineStageFlags dstStage,
                                         VkAccessFlags srcAccess,
                                         VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

bool VulkanComputePipeline::loadShader(const std::string& filepath) {
    // Try multiple search paths for shader
    std::vector<std::string> searchPaths = {
        filepath,
        "./shaders/" + filepath,
        "../shaders/" + filepath,
        "shaders/" + filepath,
        "./bin/shaders/" + filepath,
        "../bin/shaders/" + filepath
    };

    std::vector<uint32_t> code;
    bool found = false;

    for (const auto& path : searchPaths) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            code.resize(fileSize / sizeof(uint32_t));

            file.seekg(0);
            file.read(reinterpret_cast<char*>(code.data()), fileSize);
            file.close();

            LOG_INFO("ComputePipeline", "Loaded shader from: %s (%zu bytes)", path.c_str(), fileSize);
            found = true;
            break;
        }
    }

    if (!found) {
        LOG_ERROR("ComputePipeline", "Shader file not found: %s", filepath.c_str());
        return false;
    }

    return createShaderModule(code);
}

bool VulkanComputePipeline::createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule_) != VK_SUCCESS) {
        LOG_ERROR("ComputePipeline", "Failed to create shader module");
        return false;
    }

    return true;
}

} // namespace vulkan
} // namespace rendering
} // namespace jupiter
