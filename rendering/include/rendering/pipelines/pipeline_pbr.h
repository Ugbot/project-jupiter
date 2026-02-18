#pragma once

/**
 * @file pipeline_pbr.h
 * @brief Physically-Based Rendering Pipeline
 * 
 * Forward PBR renderer using Cook-Torrance BRDF.
 * Supports image-based lighting (IBL), multiple lights, and full PBR materials.
 * 
 * Features:
 * - Cook-Torrance microfacet BRDF
 * - GGX normal distribution
 * - Schlick-GGX geometry function
 * - Fresnel-Schlick approximation
 * - Image-based lighting (IBL)
 * - Multiple point/spot/directional lights
 * - Full PBR material support (albedo, normal, metallic, roughness, AO, emissive)
 * - HDR rendering with tonemapping
 */

#include "rendering/ghi/ghi.h"
#include "rendering/ral/ral_types.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>

namespace jupiter {
namespace rendering {

/**
 * @brief Maximum number of lights in PBR pipeline
 */
static constexpr uint32_t MAX_PBR_LIGHTS = 16;

/**
 * @brief PBR rendering pipeline
 * 
 * Full physically-based renderer with Cook-Torrance BRDF.
 */
class PBRPipeline {
public:
    PBRPipeline();
    ~PBRPipeline();

    /**
     * @brief Initialize pipeline
     * 
     * @param backend Active GHI backend
     * @return true if successful
     */
    bool initialize(ghi::Backend backend);

    /**
     * @brief Shutdown and cleanup
     */
    void shutdown();

    /**
     * @brief Begin frame rendering
     */
    void beginFrame();

    /**
     * @brief End frame rendering
     */
    void endFrame();

    /**
     * @brief Render a mesh with PBR material
     * 
     * @param mesh Mesh handle
     * @param transform Model matrix
     * @param material Material handle
     */
    void renderMesh(ral::MeshHandle mesh, const glm::mat4& transform, ral::MaterialHandle material);

    /**
     * @brief Set camera
     */
    void setCamera(const ral::CameraInfo& camera);

    /**
     * @brief Add directional light
     */
    void setDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity);

    /**
     * @brief Add point light
     * @return Index of the light (for updating/removing)
     */
    uint32_t addPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius);

    /**
     * @brief Add spot light
     * @return Index of the light (for updating/removing)
     */
    uint32_t addSpotLight(const glm::vec3& position, const glm::vec3& direction,
                          const glm::vec3& color, float intensity,
                          float innerCone, float outerCone, float radius);

    /**
     * @brief Remove a light by index
     */
    void removeLight(uint32_t index);

    /**
     * @brief Clear all lights
     */
    void clearLights();

    /**
     * @brief Set ambient light / IBL contribution
     */
    void setAmbientLight(const glm::vec3& color, float intensity);

    /**
     * @brief Load HDR environment map for IBL
     * 
     * @param hdrPath Path to equirectangular HDR image
     * @return true if successful
     */
    bool loadEnvironment(const char* hdrPath);

    /**
     * @brief Enable/disable IBL
     */
    void setIBLEnabled(bool enabled);

    /**
     * @brief Set exposure for HDR tonemapping
     */
    void setExposure(float exposure);

    /**
     * @brief Set gamma for gamma correction
     */
    void setGamma(float gamma);

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief Check if pipeline has a valid shader
     */
    bool hasValidShader() const { return hasValidShader_; }

private:
    bool initialized_ = false;
    bool hasValidShader_ = false;
    ghi::Backend backend_ = ghi::Backend::Vulkan;

    // Shaders
    ghi::ShaderHandle pbrShader_;
    ghi::ShaderHandle equirectToCubeShader_;
    ghi::ShaderHandle irradianceShader_;
    ghi::ShaderHandle prefilterShader_;
    ghi::ShaderHandle brdfLUTShader_;

    // Camera uniform buffer
    ghi::BufferHandle cameraUBO_;

    // Light types
    enum class LightType : uint32_t {
        Disabled = 0,
        Directional = 1,
        Point = 2,
        Spot = 3
    };

    // GPU-side light structure (16 floats = 64 bytes per light)
    struct GPULight {
        glm::vec4 positionType;      // xyz = position, w = type
        glm::vec4 directionIntensity; // xyz = direction, w = intensity
        glm::vec4 colorRadius;        // rgb = color, a = radius
        glm::vec4 coneAngles;         // x = innerCone, y = outerCone, zw = reserved
    };

    // Lighting uniform buffer
    ghi::BufferHandle lightingUBO_;

    // Material uniform buffer (per-draw)
    ghi::BufferHandle materialUBO_;

    // IBL textures
    ghi::TextureHandle environmentCubemap_;
    ghi::TextureHandle irradianceMap_;
    ghi::TextureHandle prefilterMap_;
    ghi::TextureHandle brdfLUT_;

    // Camera data (aligned to 256 bytes for Vulkan) - full PBR version
    struct alignas(256) CameraData {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::vec4 cameraPosition;  // xyz = position, w = unused
    } cameraData_;

    // Lighting data
    struct alignas(256) LightingData {
        GPULight lights[MAX_PBR_LIGHTS];
        glm::vec4 ambientColor;     // rgb = color, a = intensity
        glm::vec4 params;           // x = numLights, y = iblEnabled, z = exposure, w = gamma
    } lightingData_;

    // Material data (per-draw)
    struct alignas(256) MaterialData {
        glm::vec4 albedo;           // rgb = color, a = alpha
        glm::vec4 metallicRoughness; // x = metallic, y = roughness, zw = reserved
        glm::vec4 emissive;         // rgb = emissive color, a = occlusion strength
        glm::vec4 flags;            // x = hasAlbedoTex, y = hasNormalTex, z = hasMetRoughTex, w = hasEmissiveTex
    } materialData_;

    // Current camera
    ral::CameraInfo currentCamera_;

    // Light management
    std::array<GPULight, MAX_PBR_LIGHTS> lights_;
    uint32_t numLights_ = 0;
    uint32_t directionalLightIndex_ = UINT32_MAX;

    // Settings
    bool iblEnabled_ = false;
    float exposure_ = 1.0f;
    float gamma_ = 2.2f;

    // Helpers
    bool loadShaders();
    bool createUniformBuffers();
    bool generateIBLTextures();
    void updateCameraUBO();
    void updateLightingUBO();
    void updateMaterialUBO(ral::MaterialHandle material);
};

} // namespace rendering
} // namespace jupiter
