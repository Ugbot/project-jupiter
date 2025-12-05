#pragma once

#include "lighting.h"
#include "camera.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>

namespace jupiter {
namespace rendering {

// Forward declaration
namespace vulkan {
    class VulkanBuffer;
}

/**
 * @brief Shadow/Effects UBO for global rendering effects
 * 
 * Contains light space matrix and effect enable flags.
 * Used by PBR shader to apply shadows and SSAO.
 */
struct ShadowEffectsUBO {
    alignas(16) glm::mat4 lightSpaceMatrix;  // For shadow mapping
    alignas(16) glm::vec4 shadowParams;       // x=bias, y=normalBias, z=pcfRadius, w=unused
    alignas(4)  int32_t shadowEnabled;        // 0=disabled, 1=enabled
    alignas(4)  int32_t ssaoEnabled;          // 0=disabled, 1=enabled
    alignas(4)  float ssaoIntensity;          // SSAO strength (0-1)
    alignas(4)  int32_t _padding0;
};

/**
 * @brief Global shader uniforms (camera + lights + effects)
 *
 * Manages UBOs and descriptor sets for global rendering state.
 * Corresponds to Set 0 in shaders (scene-wide data).
 *
 * Following CLAUDE.md:
 * - Pre-allocated buffers (one per frame in flight)
 * - No runtime allocations during updates
 * - Lock-free (single-threaded updates)
 *
 * Descriptor Set 0 Layout (Global):
 * - Binding 0: Camera UBO (view/projection matrices)
 * - Binding 1: Lights UBO (light array + count)
 * - Binding 2-4: IBL textures (irradiance, prefiltered, BRDF LUT)
 * - Binding 5: Shadow map texture
 * - Binding 6: SSAO texture
 * - Binding 7: Shadow/Effects UBO (light space matrix, enable flags)
 *
 * Benefits:
 * - Centralized global state management
 * - Automatic per-frame buffering
 * - Easy camera/light updates
 * - Runtime toggleable shadow/SSAO effects
 *
 * Usage:
 * @code
 * RenderGlobals globals;
 * globals.initialize(device, allocator, 2);  // 2 frames in flight
 *
 * globals.updateCamera(frameIndex, camera);
 * globals.updateLights(frameIndex, lightManager);
 * globals.updateShadowEffects(frameIndex, lightSpaceMatrix, true, true, 1.0f);
 *
 * VkDescriptorSet globalSet = globals.getDescriptorSet(frameIndex);
 * vkCmdBindDescriptorSets(..., 0, 1, &globalSet, ...);
 * @endcode
 */
class RenderGlobals {
public:
    /**
     * @brief Camera uniform buffer data (std140 layout)
     */
    struct CameraUBO {
        alignas(16) float view[16];        // View matrix (4x4)
        alignas(16) float projection[16];  // Projection matrix (4x4)
        alignas(16) float cameraPosition[3];   // Camera world position
        alignas(4)  float _padding0;
    };

    /**
     * @brief Lighting uniform buffer data (std140 layout)
     * Matches shader layout: vec4 lightPositions[16], vec4 lightColors[16], vec4 ambientColor, int numLights
     */
    struct LightingUBO {
        alignas(16) float lightPositions[MAX_LIGHTS][4];  // xyz=pos/direction, w=type (0=directional, 1=point)
        alignas(16) float lightColors[MAX_LIGHTS][4];     // xyz=color, w=intensity
        alignas(16) float ambientColor[4];                // xyz=color, w=intensity (NEW: ambient light support)
        alignas(4)  int32_t lightCount;                   // Active light count
        alignas(4)  int32_t _padding0[3];
    };

    RenderGlobals();
    ~RenderGlobals();

    // Non-copyable
    RenderGlobals(const RenderGlobals&) = delete;
    RenderGlobals& operator=(const RenderGlobals&) = delete;

    /**
     * @brief Initialize render globals
     *
     * Creates descriptor set layout, pool, and per-frame UBOs.
     *
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param framesInFlight Number of frames in flight (typically 2)
     * @return true if successful
     */
    bool initialize(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight);

    /**
     * @brief Cleanup all resources
     */
    void destroy();

    /**
     * @brief Update camera uniforms for a frame
     *
     * @param frameIndex Frame index (0 to framesInFlight-1)
     * @param camera Camera instance
     * @return true if successful
     */
    bool updateCamera(uint32_t frameIndex, const Camera& camera);

    /**
     * @brief Update lighting uniforms for a frame
     *
     * @param frameIndex Frame index (0 to framesInFlight-1)
     * @param lightManager Light manager instance
     * @return true if successful
     */
    bool updateLights(uint32_t frameIndex, const LightManager& lightManager);

    /**
     * @brief Update IBL sampler bindings for all descriptor sets
     *
     * Call this once after IBL resources are created/changed.
     * Updates all frames with the same IBL samplers (shared resource).
     *
     * @param irradianceView Image view for irradiance cubemap
     * @param irradianceSampler Sampler for irradiance cubemap
     * @param prefilteredView Image view for prefiltered environment cubemap
     * @param prefilteredSampler Sampler for prefiltered environment cubemap
     * @param brdfLUTView Image view for BRDF LUT
     * @param brdfLUTSampler Sampler for BRDF LUT
     * @return true if successful
     */
    bool updateIBLResources(VkImageView irradianceView, VkSampler irradianceSampler,
                           VkImageView prefilteredView, VkSampler prefilteredSampler,
                           VkImageView brdfLUTView, VkSampler brdfLUTSampler);

    /**
     * @brief Update shadow map texture binding
     * 
     * Call when shadow map is created or recreated.
     * 
     * @param shadowMapView Shadow map image view
     * @param shadowSampler Shadow sampler (with depth comparison)
     * @return true if successful
     */
    bool updateShadowMap(VkImageView shadowMapView, VkSampler shadowSampler);

    /**
     * @brief Update SSAO texture binding
     * 
     * Call when SSAO texture is created or recreated.
     * 
     * @param ssaoView SSAO texture image view
     * @param ssaoSampler SSAO sampler
     * @return true if successful
     */
    bool updateSSAOTexture(VkImageView ssaoView, VkSampler ssaoSampler);

    /**
     * @brief Update shadow/effects UBO for a frame
     * 
     * @param frameIndex Frame index
     * @param lightSpaceMatrix Light space transformation matrix
     * @param shadowEnabled Enable shadow sampling in shader
     * @param ssaoEnabled Enable SSAO sampling in shader
     * @param ssaoIntensity SSAO effect strength (0-1)
     * @param shadowBias Shadow bias to prevent acne
     * @return true if successful
     */
    bool updateShadowEffects(uint32_t frameIndex,
                             const glm::mat4& lightSpaceMatrix,
                             bool shadowEnabled,
                             bool ssaoEnabled,
                             float ssaoIntensity = 1.0f,
                             float shadowBias = 0.005f);

    /**
     * @brief Get descriptor set for a frame
     *
     * @param frameIndex Frame index (0 to framesInFlight-1)
     * @return Descriptor set (Set 0: camera + lights)
     */
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const;

    /**
     * @brief Get descriptor set layout
     *
     * Used when creating pipeline layouts.
     *
     * @return Descriptor set layout for Set 0
     */
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_; }

    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    // Per-frame resources
    uint32_t framesInFlight_ = 0;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<vulkan::VulkanBuffer*> cameraBuffers_;
    std::vector<vulkan::VulkanBuffer*> lightingBuffers_;
    std::vector<vulkan::VulkanBuffer*> shadowEffectsBuffers_;

    // Shadow/SSAO state
    bool shadowMapBound_ = false;
    bool ssaoTextureBound_ = false;

    // Helper: Create descriptor set layout
    bool createDescriptorSetLayout();

    // Helper: Create descriptor pool
    bool createDescriptorPool();

    // Helper: Create uniform buffers
    bool createUniformBuffers();

    // Helper: Create and update descriptor sets
    bool createDescriptorSets();
};

} // namespace rendering
} // namespace jupiter
