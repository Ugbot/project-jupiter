#pragma once

#include "pipeline_base.h"
#include "resources_gbuffer.h"
#include "render_globals.h"

namespace jupiter::rendering {

struct DeferredPushConstants {
    alignas(16) glm::vec4 viewPos;
    float directLightIntensity;
    float ambientIntensity;
    float shadowIntensity;
    float exposure;
    float maxReflectionLod;
    float lightFalloff;
    float albedoMultiplier;
    uint32_t flags;
};

class PipelineDeferred : public PipelineBase {
public:
    PipelineDeferred(VkDevice device,
                     VkPhysicalDevice physicalDevice,
                     const PipelineConfig& config,
                     ResourcesGBuffer* gbuffer,
                     RenderGlobals* renderGlobals,
                     VkRenderPass renderPass);
                     
    ~PipelineDeferred() override;

    void fillCommandBuffer(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void onWindowResized(uint32_t width, uint32_t height) override;
    void setCameraUBO(const CameraUBO& ubo, uint32_t frameIndex) override; // Optional use

    DeferredPushConstants& getPushConstantsMutable() { return pushConstants_; }
    const DeferredPushConstants& getPushConstants() const { return pushConstants_; }

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createPipeline();
    void updateDescriptorSets();

    ResourcesGBuffer* gbuffer_;
    RenderGlobals* renderGlobals_;
    DeferredPushConstants pushConstants_{};
    
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE; // Set 1 layout
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_; // Set 1 sets (per frame, or just one if textures are static)
};

} // namespace jupiter::rendering
