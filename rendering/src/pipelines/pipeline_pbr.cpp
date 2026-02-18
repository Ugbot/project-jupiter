/**
 * @file pipeline_pbr.cpp
 * @brief PBR Pipeline Implementation
 * 
 * Cook-Torrance BRDF implementation for physically-based rendering.
 */

#include "rendering/pipelines/pipeline_pbr.h"
#include "rendering/ral/ral.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <cstring>

namespace jupiter {
namespace rendering {

// ============================================================================
// Constructor / Destructor
// ============================================================================

PBRPipeline::PBRPipeline() {
    std::memset(&cameraData_, 0, sizeof(cameraData_));
    std::memset(&lightingData_, 0, sizeof(lightingData_));
    std::memset(&materialData_, 0, sizeof(materialData_));
    
    // Default lighting parameters
    lightingData_.ambientColor = glm::vec4(0.03f, 0.03f, 0.03f, 0.5f);
    lightingData_.params = glm::vec4(0.0f, 0.0f, 1.0f, 2.2f);  // numLights, iblEnabled, exposure, gamma
    
    // Initialize light array
    for (auto& light : lights_) {
        light.positionType = glm::vec4(0.0f);  // Disabled
    }
}

PBRPipeline::~PBRPipeline() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool PBRPipeline::initialize(ghi::Backend backend) {
    if (initialized_) {
        LOG_WARN("PBRPipeline", "Already initialized");
        return true;
    }
    
    backend_ = backend;
    LOG_INFO("PBRPipeline", "Initializing PBR pipeline for backend: %d", (int)backend);
    
    if (!createUniformBuffers()) {
        LOG_ERROR("PBRPipeline", "Failed to create uniform buffers");
        return false;
    }
    
    if (!loadShaders()) {
        LOG_ERROR("PBRPipeline", "Failed to load PBR shaders");
        return false;
    }
    
    // Set default camera
    currentCamera_.position = glm::vec3(0.0f, 0.0f, 5.0f);
    currentCamera_.target = glm::vec3(0.0f);
    currentCamera_.up = glm::vec3(0.0f, 1.0f, 0.0f);
    currentCamera_.fov = 60.0f;
    currentCamera_.aspectRatio = 16.0f / 9.0f;
    currentCamera_.nearPlane = 0.1f;
    currentCamera_.farPlane = 1000.0f;
    
    // Update camera matrices
    cameraData_.view = glm::lookAt(currentCamera_.position, currentCamera_.target, currentCamera_.up);
    cameraData_.projection = glm::perspective(
        glm::radians(currentCamera_.fov),
        currentCamera_.aspectRatio,
        currentCamera_.nearPlane,
        currentCamera_.farPlane
    );
    cameraData_.viewProjection = cameraData_.projection * cameraData_.view;
    cameraData_.cameraPosition = glm::vec4(currentCamera_.position, 1.0f);
    
    updateCameraUBO();
    
    // Set default directional light
    setDirectionalLight(glm::vec3(-0.5f, -1.0f, -0.3f), glm::vec3(1.0f), 1.0f);
    
    initialized_ = true;
    LOG_INFO("PBRPipeline", "PBR pipeline initialized successfully");
    return true;
}

void PBRPipeline::shutdown() {
    if (!initialized_) return;
    
    LOG_INFO("PBRPipeline", "Shutting down PBR pipeline");
    
    // Destroy shaders
    if (pbrShader_.isValid()) {
        ghi::destroyShader(pbrShader_);
        pbrShader_ = ghi::ShaderHandle{};
    }
    
    // Destroy uniform buffers
    if (cameraUBO_.isValid()) {
        ghi::destroyBuffer(cameraUBO_);
        cameraUBO_ = ghi::BufferHandle{};
    }
    
    if (lightingUBO_.isValid()) {
        ghi::destroyBuffer(lightingUBO_);
        lightingUBO_ = ghi::BufferHandle{};
    }
    
    if (materialUBO_.isValid()) {
        ghi::destroyBuffer(materialUBO_);
        materialUBO_ = ghi::BufferHandle{};
    }
    
    // Destroy IBL textures
    if (environmentCubemap_.isValid()) {
        ghi::destroyTexture(environmentCubemap_);
        environmentCubemap_ = ghi::TextureHandle{};
    }
    
    if (irradianceMap_.isValid()) {
        ghi::destroyTexture(irradianceMap_);
        irradianceMap_ = ghi::TextureHandle{};
    }
    
    if (prefilterMap_.isValid()) {
        ghi::destroyTexture(prefilterMap_);
        prefilterMap_ = ghi::TextureHandle{};
    }
    
    if (brdfLUT_.isValid()) {
        ghi::destroyTexture(brdfLUT_);
        brdfLUT_ = ghi::TextureHandle{};
    }
    
    initialized_ = false;
}

// ============================================================================
// Shader Loading
// ============================================================================

bool PBRPipeline::loadShaders() {
    // Use SPIR-V shaders for all backends - GHI will convert to MSL for Metal
    // Shader path is relative to executable (bin/shaders/pbr/)
    ghi::ShaderSource source;
    source.vertexPath = "shaders/pbr/pbr.vert.spv";
    source.fragmentPath = "shaders/pbr/pbr.frag.spv";
    
    pbrShader_ = ghi::createShader(source);
    if (!pbrShader_.isValid()) {
        LOG_ERROR("PBRPipeline", "Failed to load PBR shader - PBR pipeline disabled");
        hasValidShader_ = false;
        return true;  // Don't fail init, just disable rendering
    }
    
    hasValidShader_ = true;
    LOG_INFO("PBRPipeline", "Loaded PBR shaders for PBR pipeline");
    return true;
}

// ============================================================================
// Uniform Buffer Creation
// ============================================================================

bool PBRPipeline::createUniformBuffers() {
    // Camera UBO (matches rendering/shaders/pbr/pbr.vert)
    ghi::BufferCreateInfo cameraInfo;
    cameraInfo.type = ghi::BufferType::Uniform;
    cameraInfo.usage = ghi::BufferUsage::Dynamic;
    cameraInfo.size = sizeof(CameraData);
    cameraInfo.data = nullptr;
    
    cameraUBO_ = ghi::createBuffer(cameraInfo);
    if (!cameraUBO_.isValid()) {
        LOG_ERROR("PBRPipeline", "Failed to create camera UBO");
        return false;
    }
    
    // These UBOs are for future full-PBR shader support
    // Lighting UBO
    ghi::BufferCreateInfo lightingInfo;
    lightingInfo.type = ghi::BufferType::Uniform;
    lightingInfo.usage = ghi::BufferUsage::Dynamic;
    lightingInfo.size = sizeof(LightingData);
    lightingInfo.data = nullptr;
    
    lightingUBO_ = ghi::createBuffer(lightingInfo);
    if (!lightingUBO_.isValid()) {
        LOG_ERROR("PBRPipeline", "Failed to create lighting UBO");
        return false;
    }
    
    // Material UBO
    ghi::BufferCreateInfo materialInfo;
    materialInfo.type = ghi::BufferType::Uniform;
    materialInfo.usage = ghi::BufferUsage::Dynamic;
    materialInfo.size = sizeof(MaterialData);
    materialInfo.data = nullptr;
    
    materialUBO_ = ghi::createBuffer(materialInfo);
    if (!materialUBO_.isValid()) {
        LOG_ERROR("PBRPipeline", "Failed to create material UBO");
        return false;
    }
    
    LOG_INFO("PBRPipeline", "Created uniform buffers: camera=%zu, lighting=%zu, material=%zu bytes",
             sizeof(CameraData), sizeof(LightingData), sizeof(MaterialData));
    
    return true;
}

// ============================================================================
// Frame Rendering
// ============================================================================

void PBRPipeline::beginFrame() {
    if (!initialized_ || !hasValidShader_) return;
    
    ghi::beginFrame();
    ghi::beginRenderPass();
    
    // Set viewport (same as SimplePipeline)
    uint32_t width = 1280;  // TODO: Get from swapchain
    uint32_t height = 720;
    ghi::setViewport(0, 0, width, height);
    ghi::setScissor(0, 0, width, height);
    
    // Bind shader
    ghi::RenderState state;
    state.shader = pbrShader_;
    state.depthTestEnabled = true;
    state.depthWriteEnabled = true;
    state.cullFaceEnabled = true;
    state.cullMode = ghi::CullMode::Back;
    ghi::setRenderState(state);
    
    // Bind UBOs (match pbr shader bindings)
    // set 0 binding 0: camera
    ghi::bindUniformBuffer(cameraUBO_, 0, 0);
    // set 1 binding 0: lighting
    ghi::bindUniformBuffer(lightingUBO_, 1, 0);
}

void PBRPipeline::endFrame() {
    if (!initialized_ || !hasValidShader_) return;
    
    ghi::endRenderPass();
    ghi::endFrame();
}

// ============================================================================
// Mesh Rendering
// ============================================================================

void PBRPipeline::renderMesh(ral::MeshHandle mesh, const glm::mat4& transform, ral::MaterialHandle material) {
    if (!initialized_ || !hasValidShader_) return;

    // Update + bind material UBO (matches pbr shader set 1 binding 1)
    updateMaterialUBO(material);
    ghi::bindUniformBuffer(materialUBO_, 1, 1);

    // Push constants: model matrix only (matches rendering/shaders/pbr/pbr.vert)
    ghi::setPushConstants(&transform, sizeof(glm::mat4), 0);
}

// ============================================================================
// Camera
// ============================================================================

void PBRPipeline::setCamera(const ral::CameraInfo& camera) {
    currentCamera_ = camera;
    
    // Update full PBR camera data
    cameraData_.view = glm::lookAt(camera.position, camera.target, camera.up);
    cameraData_.projection = glm::perspective(
        glm::radians(camera.fov),
        camera.aspectRatio,
        camera.nearPlane,
        camera.farPlane
    );
    cameraData_.viewProjection = cameraData_.projection * cameraData_.view;
    cameraData_.cameraPosition = glm::vec4(camera.position, 1.0f);
    
    updateCameraUBO();
}

void PBRPipeline::updateCameraUBO() {
    if (cameraUBO_.isValid()) {
        ghi::updateBuffer(cameraUBO_, 0, sizeof(CameraData), &cameraData_);
    }
}

// ============================================================================
// Lighting
// ============================================================================

void PBRPipeline::setDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity) {
    // Find or create directional light slot
    if (directionalLightIndex_ == UINT32_MAX) {
        if (numLights_ >= MAX_PBR_LIGHTS) {
            LOG_WARN("PBRPipeline", "Max lights reached, cannot add directional light");
            return;
        }
        directionalLightIndex_ = numLights_++;
    }
    
    GPULight& light = lights_[directionalLightIndex_];
    light.positionType = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(LightType::Directional));
    light.directionIntensity = glm::vec4(glm::normalize(direction), intensity);
    light.colorRadius = glm::vec4(color, 0.0f);
    light.coneAngles = glm::vec4(0.0f);

    updateLightingUBO();
}

uint32_t PBRPipeline::addPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius) {
    if (numLights_ >= MAX_PBR_LIGHTS) {
        LOG_WARN("PBRPipeline", "Max lights reached, cannot add point light");
        return UINT32_MAX;
    }
    
    uint32_t index = numLights_++;
    
    GPULight& light = lights_[index];
    light.positionType = glm::vec4(position, static_cast<float>(LightType::Point));
    light.directionIntensity = glm::vec4(0.0f, 0.0f, 0.0f, intensity);
    light.colorRadius = glm::vec4(color, radius);
    light.coneAngles = glm::vec4(0.0f);
    
    updateLightingUBO();
    return index;
}

uint32_t PBRPipeline::addSpotLight(const glm::vec3& position, const glm::vec3& direction,
                                    const glm::vec3& color, float intensity,
                                    float innerCone, float outerCone, float radius) {
    if (numLights_ >= MAX_PBR_LIGHTS) {
        LOG_WARN("PBRPipeline", "Max lights reached, cannot add spot light");
        return UINT32_MAX;
    }
    
    uint32_t index = numLights_++;
    
    GPULight& light = lights_[index];
    light.positionType = glm::vec4(position, static_cast<float>(LightType::Spot));
    light.directionIntensity = glm::vec4(glm::normalize(direction), intensity);
    light.colorRadius = glm::vec4(color, radius);
    light.coneAngles = glm::vec4(std::cos(innerCone), std::cos(outerCone), 0.0f, 0.0f);
    
    updateLightingUBO();
    return index;
}

void PBRPipeline::removeLight(uint32_t index) {
    if (index >= numLights_) return;
    
    // Mark as disabled
    lights_[index].positionType.w = static_cast<float>(LightType::Disabled);
    
    // If this was the directional light, clear the index
    if (index == directionalLightIndex_) {
        directionalLightIndex_ = UINT32_MAX;
    }
    
    updateLightingUBO();
}

void PBRPipeline::clearLights() {
    for (auto& light : lights_) {
        light.positionType = glm::vec4(0.0f);
    }
    numLights_ = 0;
    directionalLightIndex_ = UINT32_MAX;
    
    updateLightingUBO();
}

void PBRPipeline::setAmbientLight(const glm::vec3& color, float intensity) {
    lightingData_.ambientColor = glm::vec4(color, intensity);
    updateLightingUBO();
}

void PBRPipeline::updateLightingUBO() {
    // Copy lights array
    std::memcpy(lightingData_.lights, lights_.data(), sizeof(GPULight) * MAX_PBR_LIGHTS);
    
    // Update params
    lightingData_.params.x = static_cast<float>(numLights_);
    lightingData_.params.y = iblEnabled_ ? 1.0f : 0.0f;
    lightingData_.params.z = exposure_;
    lightingData_.params.w = gamma_;
    
    if (lightingUBO_.isValid()) {
        ghi::updateBuffer(lightingUBO_, 0, sizeof(LightingData), &lightingData_);
    }
}

// ============================================================================
// Material
// ============================================================================

void PBRPipeline::updateMaterialUBO(ral::MaterialHandle material) {
    static const bool debugPipeline =
        (std::getenv("JUPITER_DEBUG_PIPELINE") != nullptr);

    // Get material data from RAL
    glm::vec3 baseColor = ral::getMaterialBaseColor(material);
    float metallic = ral::getMaterialMetallic(material);
    float roughness = ral::getMaterialRoughness(material);
    glm::vec3 emissive = ral::getMaterialEmissive(material);
    
    // Fill material data
    materialData_.albedo = glm::vec4(baseColor, 1.0f);
    materialData_.metallicRoughness = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    materialData_.emissive = glm::vec4(emissive, 1.0f);
    materialData_.flags = glm::vec4(0.0f);  // No textures for now
    materialData_.flags.w = debugPipeline ? 1.0f : 0.0f;
    
    // TODO: Handle material textures
    // Check for albedo texture, normal map, metallic-roughness map, etc.
    // and set appropriate flags
    
    if (materialUBO_.isValid()) {
        ghi::updateBuffer(materialUBO_, 0, sizeof(MaterialData), &materialData_);
    }
}

// ============================================================================
// IBL
// ============================================================================

bool PBRPipeline::loadEnvironment(const char* hdrPath) {
    LOG_INFO("PBRPipeline", "Loading environment: %s", hdrPath);
    
    // TODO: Implement HDR loading and IBL texture generation
    // This requires:
    // 1. Load HDR equirectangular image
    // 2. Convert to cubemap
    // 3. Generate irradiance map (diffuse IBL)
    // 4. Generate prefiltered environment map (specular IBL)
    // 5. Generate BRDF LUT
    
    return false;
}

void PBRPipeline::setIBLEnabled(bool enabled) {
    iblEnabled_ = enabled;
    updateLightingUBO();
}

void PBRPipeline::setExposure(float exposure) {
    exposure_ = exposure;
    updateLightingUBO();
}

void PBRPipeline::setGamma(float gamma) {
    gamma_ = gamma;
    updateLightingUBO();
}

} // namespace rendering
} // namespace jupiter
