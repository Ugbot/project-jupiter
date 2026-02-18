#pragma once

/**
 * @file ral.h
 * @brief Render Abstraction Layer - Core API
 * 
 * High-level rendering API built on GHI.
 * Provides mesh/material/light management, pipeline selection, and scene rendering.
 * 
 * Based on Venus RAL patterns + HelloVulkan Scene organization.
 * 
 * Usage:
 * @code
 * ral::initialize();
 * ral::usePipeline(ral::Pipeline::PBR);
 * 
 * auto mesh = ral::createCube(10.0f);
 * auto material = ral::createPBRMaterial(glm::vec3(0.8, 0.2, 0.2), 0.1f, 0.8f);
 * ral::renderMesh(mesh, glm::mat4(1.0f), material);
 * @endcode
 */

#include "ral_types.h"

namespace jupiter {
namespace rendering {
namespace ral {

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize RAL
 * 
 * Must be called after GHI initialization.
 * 
 * @return true if successful
 */
bool initialize();

/**
 * @brief Shutdown RAL and cleanup resources
 */
void shutdown();

/**
 * @brief Check if RAL is initialized
 */
bool isInitialized();

// ============================================================================
// Pipeline Selection
// ============================================================================

/**
 * @brief Select active rendering pipeline
 * 
 * Different pipelines for different use cases:
 * - Simple: Forward Lambertian (always available)
 * - PBR: Physically-based with IBL
 * - Deferred: Many lights, G-Buffer
 * - Clustered: Hundreds of lights
 * - Voxel: Specialized voxel rendering
 * 
 * @param pipeline Pipeline to use
 * @return true if pipeline available and activated
 */
bool usePipeline(Pipeline pipeline);

/**
 * @brief Get active pipeline
 */
Pipeline getActivePipeline();

/**
 * @brief Check if pipeline is available
 * 
 * Some pipelines require features not all backends support.
 * 
 * @param pipeline Pipeline to check
 * @return true if available on current backend
 */
bool isPipelineAvailable(Pipeline pipeline);

// ============================================================================
// Mesh Management
// ============================================================================

/**
 * @brief Create mesh from vertices and indices
 * 
 * @param info Mesh creation parameters
 * @return Mesh handle (check .isValid())
 */
MeshHandle createMesh(const MeshInfo& info);

/**
 * @brief Destroy mesh
 */
void destroyMesh(MeshHandle handle);

/**
 * @brief Get mesh vertex buffer (for custom rendering)
 */
ghi::BufferHandle getMeshVertexBuffer(MeshHandle handle);

/**
 * @brief Get mesh index buffer (for custom rendering)
 */
ghi::BufferHandle getMeshIndexBuffer(MeshHandle handle);

/**
 * @brief Get mesh index count (for draw calls)
 */
uint32_t getMeshIndexCount(MeshHandle handle);

/**
 * @brief Get mesh vertex count
 */
uint32_t getMeshVertexCount(MeshHandle handle);

/**
 * @brief Get material base color (for simple materials)
 */
glm::vec3 getMaterialBaseColor(MaterialHandle handle);

/**
 * @brief Get material metallic factor (for PBR materials)
 */
float getMaterialMetallic(MaterialHandle handle);

/**
 * @brief Get material roughness factor (for PBR materials)
 */
float getMaterialRoughness(MaterialHandle handle);

/**
 * @brief Get material emissive color
 */
glm::vec3 getMaterialEmissive(MaterialHandle handle);

/**
 * @brief Check if material is PBR type
 */
bool isMaterialPBR(MaterialHandle handle);

// Built-in mesh helpers (Venus pattern)
MeshHandle createCube(float size);
MeshHandle createSphere(float radius, uint32_t segments = 32);
MeshHandle createPlane(float width, float height, uint32_t segmentsX = 1, uint32_t segmentsZ = 1);
MeshHandle createCylinder(float radius, float height, uint32_t segments = 32);
MeshHandle createCapsule(float radius, float height, uint32_t segments = 32);

// ============================================================================
// Material Management
// ============================================================================

/**
 * @brief Create material
 * 
 * @param info Material parameters
 * @return Material handle (check .isValid())
 */
MaterialHandle createMaterial(const MaterialInfo& info);

/**
 * @brief Destroy material
 */
void destroyMaterial(MaterialHandle handle);

// Material helpers
MaterialHandle createSimpleMaterial(glm::vec3 color);
MaterialHandle createTexturedMaterial(ghi::TextureHandle albedo);
MaterialHandle createPBRMaterial(glm::vec3 albedo, float metallic, float roughness);
MaterialHandle createPBRMaterialTextured(
    ghi::TextureHandle albedo,
    ghi::TextureHandle normal = {},
    ghi::TextureHandle metallicRoughness = {},
    ghi::TextureHandle occlusion = {},
    ghi::TextureHandle emissive = {}
);

// ============================================================================
// Light Management
// ============================================================================

/**
 * @brief Create light
 * 
 * @param info Light parameters
 * @return Light handle (check .isValid())
 */
LightHandle createLight(const LightInfo& info);

/**
 * @brief Destroy light
 */
void destroyLight(LightHandle handle);

/**
 * @brief Update light properties
 */
void updateLight(LightHandle handle, const LightInfo& info);

// Light helpers (LearnOpenGL patterns)
LightHandle createDirectionalLight(glm::vec3 direction, glm::vec3 color, float intensity = 1.0f);
LightHandle createPointLight(glm::vec3 position, glm::vec3 color, float radius = 10.0f, float intensity = 1.0f);
LightHandle createSpotLight(glm::vec3 position, glm::vec3 direction, glm::vec3 color, 
                           float innerAngle = 0.4f, float outerAngle = 0.6f, float intensity = 1.0f);

/**
 * @brief Set ambient lighting
 * 
 * @param color Ambient light color
 * @param intensity Ambient intensity
 */
void setAmbientLight(glm::vec3 color, float intensity = 1.0f);

// ============================================================================
// Camera Management
// ============================================================================

/**
 * @brief Set active camera
 * 
 * @param camera Camera parameters (RAL updates matrices)
 */
void setCamera(const CameraInfo& camera);

/**
 * @brief Get active camera
 */
const CameraInfo& getCamera();

// ============================================================================
// Rendering
// ============================================================================

/**
 * @brief Render mesh with transform and material
 * 
 * Submits to render queue for current pipeline.
 * 
 * @param mesh Mesh to render
 * @param transform Model matrix
 * @param material Material (or use mesh's default)
 */
void renderMesh(MeshHandle mesh, const glm::mat4& transform, MaterialHandle material = {});

/**
 * @brief Begin frame rendering
 * 
 * Clears render queue, begins GHI frame.
 */
void beginFrame();

/**
 * @brief End frame rendering
 * 
 * Executes render queue through active pipeline, presents frame.
 */
void endFrame();

/**
 * @brief Manually flush render queue
 * 
 * Useful for multi-pass rendering.
 */
void flushRenderQueue();

// ============================================================================
// Advanced Features (Pipeline-Dependent)
// ============================================================================

/**
 * @brief Load environment map for IBL
 * 
 * Requires: PBR pipeline
 * Generates cubemap, spherical harmonics, prefiltered map.
 * 
 * @param hdrPath Path to HDR equirectangular environment map
 * @return true if successful
 */
bool loadEnvironment(const char* hdrPath);

/**
 * @brief Enable/disable shadows
 * 
 * Requires: Pipeline with shadow support
 */
void enableShadows(bool enable);

/**
 * @brief Enable/disable SSAO
 * 
 * Requires: Deferred or PBR pipeline with SSAO support
 */
void enableSSAO(bool enable);

/**
 * @brief Enable/disable bloom
 */
void enableBloom(bool enable);

/**
 * @brief Enable/disable HDR tonemapping
 */
void enableHDR(bool enable);

/**
 * @brief Set exposure for HDR
 */
void setExposure(float exposure);

// ============================================================================
// Debug / Utility
// ============================================================================

/**
 * @brief Get rendering statistics
 */
struct Statistics {
    uint32_t meshesRendered = 0;
    uint32_t lightsActive = 0;
    uint32_t drawCallsOpaque = 0;
    uint32_t drawCallsTransparent = 0;
    uint32_t verticesRendered = 0;
    uint32_t trianglesRendered = 0;
    float frameTimeMs = 0.0f;
};

const Statistics& getStatistics();
void resetStatistics();

/**
 * @brief Enable wireframe rendering
 */
void setWireframeMode(bool enabled);

/**
 * @brief Set clear color
 */
void setClearColor(glm::vec4 color);

} // namespace ral
} // namespace rendering
} // namespace jupiter

