#include "rendering/pipeline_exposure.h"
#include "logging/logging.h"
#include <fstream>
#include <vector>
#include <cmath>

namespace jupiter {
namespace rendering {

PipelineExposure::~PipelineExposure() {
    destroy();
}

PipelineExposure::PipelineExposure(PipelineExposure&& other) noexcept
    : device_(other.device_)
    , resources_(other.resources_)
    , descriptorSetLayout_(other.descriptorSetLayout_)
    , descriptorPool_(other.descriptorPool_)
    , descriptorSets_(std::move(other.descriptorSets_))
    , histogramPipelineLayout_(other.histogramPipelineLayout_)
    , averagePipelineLayout_(other.averagePipelineLayout_)
    , adaptPipelineLayout_(other.adaptPipelineLayout_)
    , histogramPipeline_(other.histogramPipeline_)
    , averagePipeline_(other.averagePipeline_)
    , adaptPipeline_(other.adaptPipeline_)
    , hdrImageView_(other.hdrImageView_)
    , hdrSampler_(other.hdrSampler_)
    , hdrBindingDirty_(other.hdrBindingDirty_) {
    other.device_ = VK_NULL_HANDLE;
    other.descriptorSetLayout_ = VK_NULL_HANDLE;
    other.descriptorPool_ = VK_NULL_HANDLE;
    other.histogramPipelineLayout_ = VK_NULL_HANDLE;
    other.averagePipelineLayout_ = VK_NULL_HANDLE;
    other.adaptPipelineLayout_ = VK_NULL_HANDLE;
    other.histogramPipeline_ = VK_NULL_HANDLE;
    other.averagePipeline_ = VK_NULL_HANDLE;
    other.adaptPipeline_ = VK_NULL_HANDLE;
}

PipelineExposure& PipelineExposure::operator=(PipelineExposure&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        resources_ = other.resources_;
        descriptorSetLayout_ = other.descriptorSetLayout_;
        descriptorPool_ = other.descriptorPool_;
        descriptorSets_ = std::move(other.descriptorSets_);
        histogramPipelineLayout_ = other.histogramPipelineLayout_;
        averagePipelineLayout_ = other.averagePipelineLayout_;
        adaptPipelineLayout_ = other.adaptPipelineLayout_;
        histogramPipeline_ = other.histogramPipeline_;
        averagePipeline_ = other.averagePipeline_;
        adaptPipeline_ = other.adaptPipeline_;
        hdrImageView_ = other.hdrImageView_;
        hdrSampler_ = other.hdrSampler_;
        hdrBindingDirty_ = other.hdrBindingDirty_;
        
        other.device_ = VK_NULL_HANDLE;
        other.descriptorSetLayout_ = VK_NULL_HANDLE;
        other.descriptorPool_ = VK_NULL_HANDLE;
        other.histogramPipelineLayout_ = VK_NULL_HANDLE;
        other.averagePipelineLayout_ = VK_NULL_HANDLE;
        other.adaptPipelineLayout_ = VK_NULL_HANDLE;
        other.histogramPipeline_ = VK_NULL_HANDLE;
        other.averagePipeline_ = VK_NULL_HANDLE;
        other.adaptPipeline_ = VK_NULL_HANDLE;
    }
    return *this;
}

void PipelineExposure::create(VkDevice device, ResourcesExposure* resources,
                              const std::string& shaderPath) {
    device_ = device;
    resources_ = resources;

    LOG_INFO("Exposure", "Creating auto-exposure pipeline...");

    uint32_t framesInFlight = 2;  // Default to 2 frames in flight

    if (!createDescriptorSetLayout()) {
        throw std::runtime_error("Failed to create exposure descriptor set layout");
    }

    if (!createDescriptorPool(framesInFlight)) {
        throw std::runtime_error("Failed to create exposure descriptor pool");
    }

    if (!createDescriptorSets(framesInFlight)) {
        throw std::runtime_error("Failed to create exposure descriptor sets");
    }

    if (!createPipelineLayouts()) {
        throw std::runtime_error("Failed to create exposure pipeline layouts");
    }

    if (!createPipelines(shaderPath)) {
        throw std::runtime_error("Failed to create exposure compute pipelines");
    }

    LOG_INFO("Exposure", "Auto-exposure pipeline created successfully");
}

void PipelineExposure::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    // Destroy pipelines
    if (histogramPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, histogramPipeline_, nullptr);
        histogramPipeline_ = VK_NULL_HANDLE;
    }
    if (averagePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, averagePipeline_, nullptr);
        averagePipeline_ = VK_NULL_HANDLE;
    }
    if (adaptPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, adaptPipeline_, nullptr);
        adaptPipeline_ = VK_NULL_HANDLE;
    }

    // Destroy pipeline layouts
    if (histogramPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, histogramPipelineLayout_, nullptr);
        histogramPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (averagePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, averagePipelineLayout_, nullptr);
        averagePipelineLayout_ = VK_NULL_HANDLE;
    }
    if (adaptPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, adaptPipelineLayout_, nullptr);
        adaptPipelineLayout_ = VK_NULL_HANDLE;
    }

    // Destroy descriptor pool (automatically frees descriptor sets)
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    descriptorSets_.clear();

    // Destroy descriptor set layout
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
    resources_ = nullptr;
}

bool PipelineExposure::createDescriptorSetLayout() {
    // Binding 0: HDR image sampler (for histogram pass)
    // Binding 1: Exposure buffer (SSBO)
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Exposure", "Failed to create descriptor set layout: %d", result);
        return false;
    }

    return true;
}

bool PipelineExposure::createDescriptorPool(uint32_t framesInFlight) {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = framesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = framesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = framesInFlight;

    VkResult result = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Exposure", "Failed to create descriptor pool: %d", result);
        return false;
    }

    return true;
}

bool PipelineExposure::createDescriptorSets(uint32_t framesInFlight) {
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(framesInFlight);
    VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data());
    if (result != VK_SUCCESS) {
        LOG_ERROR("Exposure", "Failed to allocate descriptor sets: %d", result);
        return false;
    }

    // Initially update just the buffer bindings
    // HDR image will be updated when updateHDRBinding is called
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        VkDescriptorBufferInfo bufferInfo = resources_->getDescriptorInfo(i);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[i];
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    return true;
}

bool PipelineExposure::createPipelineLayouts() {
    // Histogram pipeline layout
    {
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(HistogramPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &histogramPipelineLayout_);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create histogram pipeline layout: %d", result);
            return false;
        }
    }

    // Average pipeline layout
    {
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(AveragePushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &averagePipelineLayout_);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create average pipeline layout: %d", result);
            return false;
        }
    }

    // Adapt pipeline layout
    {
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(AdaptPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &adaptPipelineLayout_);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create adapt pipeline layout: %d", result);
            return false;
        }
    }

    return true;
}

bool PipelineExposure::createPipelines(const std::string& shaderPath) {
    // Load and create histogram pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(shaderPath + "luminance_histogram.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            LOG_WARN("Exposure", "Failed to load luminance_histogram.comp.spv");
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = histogramPipelineLayout_;

        VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                   nullptr, &histogramPipeline_);
        vkDestroyShaderModule(device_, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create histogram pipeline: %d", result);
            return false;
        }
    }

    // Load and create average pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(shaderPath + "histogram_average.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            LOG_WARN("Exposure", "Failed to load histogram_average.comp.spv");
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = averagePipelineLayout_;

        VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                   nullptr, &averagePipeline_);
        vkDestroyShaderModule(device_, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create average pipeline: %d", result);
            return false;
        }
    }

    // Load and create adapt pipeline
    {
        VkShaderModule shaderModule = loadShaderModule(shaderPath + "exposure_adapt.comp.spv");
        if (shaderModule == VK_NULL_HANDLE) {
            LOG_WARN("Exposure", "Failed to load exposure_adapt.comp.spv");
            return false;
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = adaptPipelineLayout_;

        VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                   nullptr, &adaptPipeline_);
        vkDestroyShaderModule(device_, shaderModule, nullptr);

        if (result != VK_SUCCESS) {
            LOG_ERROR("Exposure", "Failed to create adapt pipeline: %d", result);
            return false;
        }
    }

    return true;
}

VkShaderModule PipelineExposure::loadShaderModule(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_WARN("Exposure", "Could not open shader file: %s", path.c_str());
        return VK_NULL_HANDLE;
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
    VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Exposure", "Failed to create shader module: %d", result);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

void PipelineExposure::updateHDRBinding(VkImageView hdrImageView, VkSampler hdrSampler) {
    hdrImageView_ = hdrImageView;
    hdrSampler_ = hdrSampler;
    hdrBindingDirty_ = true;
}

void PipelineExposure::updateDescriptorSets() {
    if (!hdrBindingDirty_ || hdrImageView_ == VK_NULL_HANDLE || hdrSampler_ == VK_NULL_HANDLE) {
        return;
    }

    for (uint32_t i = 0; i < descriptorSets_.size(); ++i) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = hdrImageView_;
        imageInfo.sampler = hdrSampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    hdrBindingDirty_ = false;
}

void PipelineExposure::recordHistogramPass(VkCommandBuffer cmd, uint32_t frameIndex,
                                           uint32_t imageWidth, uint32_t imageHeight) {
    if (!isInitialized() || !resources_) return;

    updateDescriptorSets();

    // Reset histogram for this frame
    resources_->resetHistogram(frameIndex);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, histogramPipelineLayout_,
                           0, 1, &descriptorSets_[frameIndex % descriptorSets_.size()],
                           0, nullptr);

    // Push constants
    const ExposureConfig& config = resources_->getConfig();
    HistogramPushConstants pc{};
    pc.minLogLuminance = config.minLogLuminance;
    pc.maxLogLuminance = config.maxLogLuminance;
    pc.invLogLuminanceRange = 1.0f / (config.maxLogLuminance - config.minLogLuminance);
    pc.pixelCount = imageWidth * imageHeight;

    vkCmdPushConstants(cmd, histogramPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Dispatch: 16x16 work groups
    uint32_t groupCountX = (imageWidth + 15) / 16;
    uint32_t groupCountY = (imageHeight + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Memory barrier for histogram buffer
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void PipelineExposure::recordAveragePass(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!isInitialized() || !resources_) return;

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, averagePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, averagePipelineLayout_,
                           0, 1, &descriptorSets_[frameIndex % descriptorSets_.size()],
                           0, nullptr);

    // Push constants
    const ExposureConfig& config = resources_->getConfig();
    AveragePushConstants pc{};
    pc.minLogLuminance = config.minLogLuminance;
    pc.maxLogLuminance = config.maxLogLuminance;
    pc.lowPercent = config.histogramLowPercent;
    pc.highPercent = config.histogramHighPercent;
    pc.targetLuminance = config.targetLuminance;
    pc.totalPixels = 0;  // Will be calculated from histogram
    pc.minExposure = config.minExposure;
    pc.maxExposure = config.maxExposure;

    vkCmdPushConstants(cmd, averagePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Single workgroup
    vkCmdDispatch(cmd, 1, 1, 1);

    // Memory barrier
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void PipelineExposure::recordAdaptPass(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime) {
    if (!isInitialized() || !resources_) return;

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, adaptPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, adaptPipelineLayout_,
                           0, 1, &descriptorSets_[frameIndex % descriptorSets_.size()],
                           0, nullptr);

    // Push constants
    const ExposureConfig& config = resources_->getConfig();
    AdaptPushConstants pc{};
    pc.deltaTime = deltaTime;
    pc.adaptSpeedUp = config.adaptationSpeed;
    pc.adaptSpeedDown = config.adaptationSpeed * 2.0f;  // Faster when going dark
    pc.minExposure = config.minExposure;
    pc.maxExposure = config.maxExposure;

    vkCmdPushConstants(cmd, adaptPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    // Single invocation
    vkCmdDispatch(cmd, 1, 1, 1);
}

void PipelineExposure::execute(VkCommandBuffer cmd, uint32_t frameIndex,
                               uint32_t imageWidth, uint32_t imageHeight, float deltaTime) {
    recordHistogramPass(cmd, frameIndex, imageWidth, imageHeight);
    recordAveragePass(cmd, frameIndex);
    recordAdaptPass(cmd, frameIndex, deltaTime);
}

float PipelineExposure::getCurrentExposure(uint32_t frameIndex) const {
    if (resources_) {
        return resources_->getCurrentExposure(frameIndex);
    }
    return 1.0f;
}

} // namespace rendering
} // namespace jupiter









