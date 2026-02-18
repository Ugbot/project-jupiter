#pragma once

/**
 * @file pipeline_simple.h
 * @brief Simple Forward Renderer Pipeline
 * 
 * Basic forward renderer with Lambertian lighting.
 * Works on all backends (Vulkan, Metal, OpenGL).
 * 
 * Features:
 * - Forward rendering (one pass)
 * - Lambertian diffuse + ambient lighting
 * - Single directional light
 * - Textured materials
 * - Depth testing
 */

#include "rendering/ghi/ghi.h"
#include "rendering/ral/ral_types.h"
#include <glm/glm.hpp>
#include <vector>

namespace jupiter {
namespace rendering {

/**
 * @brief Simple forward rendering pipeline
 * 
 * Minimal renderer that works on all backends.
 * Uses Lambertian lighting (diffuse + ambient).
 */
class SimplePipeline {
public:
    SimplePipeline();
    ~SimplePipeline();

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
     * @brief Render a mesh
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
     * @brief Set directional light
     */
    void setDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity);

    /**
     * @brief Set ambient light
     */
    void setAmbientLight(const glm::vec3& color, float intensity);

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    bool initialized_ = false;
    ghi::Backend backend_ = ghi::Backend::Vulkan;

    // Shader
    ghi::ShaderHandle shader_;

    // Uniform buffer (combined camera + lighting)
    ghi::BufferHandle cameraUBO_;
    ghi::BufferHandle lightingUBO_;  // Kept for compatibility, but we use cameraUBO for combined

    // Combined camera + lighting data (matches shader UBO layout)
    struct CameraData {
        glm::mat4 view;              // 64 bytes
        glm::mat4 projection;        // 64 bytes
        glm::vec4 sunDirIntensity;   // 16 bytes: xyz = dir, w = intensity
        glm::vec4 sunColor;          // 16 bytes: rgb = color, a = unused  
        glm::vec4 ambientColor;      // 16 bytes: rgb = color, a = intensity
    } cameraData_;                   // Total: 176 bytes

    // Push constant structure (matches shader layout)
    struct PushConstantData {
        glm::mat4 model;         // 64 bytes
        glm::vec4 baseColor;     // 16 bytes: rgb = color, a = alpha
        glm::vec4 materialProps; // 16 bytes: x = metallic, y = roughness, z = unused, w = unused
    };  // Total: 96 bytes

    // Legacy lighting data (used for storage, synced to cameraData_)
    struct LightingData {
        glm::vec4 sunDirIntensity;  // xyz = dir, w = intensity
        glm::vec4 sunColor;
        glm::vec4 ambientColor;     // rgb = color, a = intensity
        glm::vec4 padding;
    } lightingData_;

    // Current camera
    ral::CameraInfo currentCamera_;

    // Helpers
    bool loadShaders();
    void updateCameraUBO();
    void updateLightingUBO();
};

} // namespace rendering
} // namespace jupiter

