/**
 * @file pipeline_simple.cpp
 * @brief Simple Forward Renderer Implementation
 */

#include "rendering/pipelines/pipeline_simple.h"
#include "rendering/ral/ral.h"
#include "logging/logging.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>

namespace jupiter {
namespace rendering {

SimplePipeline::SimplePipeline() {
    LOG_INFO("SimplePipeline", "Simple forward pipeline created");
    
    // Set default lighting (stored in lightingData_, synced to cameraData_ on update)
    lightingData_.sunDirIntensity = glm::vec4(-0.4f, -0.8f, -0.3f, 3.0f);  // Sun from upper-right, bright
    lightingData_.sunColor = glm::vec4(1.0f, 0.95f, 0.85f, 1.0f);  // Warm sunlight
    lightingData_.ambientColor = glm::vec4(0.4f, 0.45f, 0.5f, 0.5f);  // Cool ambient
    
    // Initialize combined camera data with lighting
    cameraData_.sunDirIntensity = lightingData_.sunDirIntensity;
    cameraData_.sunColor = lightingData_.sunColor;
    cameraData_.ambientColor = lightingData_.ambientColor;
}

SimplePipeline::~SimplePipeline() {
    shutdown();
}

bool SimplePipeline::initialize(ghi::Backend backend) {
    LOG_INFO("SimplePipeline", "Initializing simple forward pipeline for backend: %s", 
             ghi::getBackendName(backend));
    
    backend_ = backend;
    
    // Create uniform buffers
    // Initialize camera data with identity matrices (will be updated later)
    cameraData_.view = glm::mat4(1.0f);
    cameraData_.projection = glm::mat4(1.0f);
    
    cameraUBO_ = ghi::createBuffer({
        .type = ghi::BufferType::Uniform,
        .usage = ghi::BufferUsage::Dynamic,
        .size = sizeof(CameraData),
        .data = &cameraData_  // Initialize with identity matrices
    });
    
    if (!cameraUBO_.isValid()) {
        LOG_ERROR("SimplePipeline", "Failed to create camera UBO");
        return false;
    }
    
    lightingUBO_ = ghi::createBuffer({
        .type = ghi::BufferType::Uniform,
        .usage = ghi::BufferUsage::Dynamic,
        .size = sizeof(LightingData),
        .data = &lightingData_
    });
    
    if (!lightingUBO_.isValid()) {
        LOG_ERROR("SimplePipeline", "Failed to create lighting UBO");
        ghi::destroyBuffer(cameraUBO_);
        return false;
    }
    
    LOG_INFO("SimplePipeline", "Created uniform buffers");
    
    // Load shaders
    if (!loadShaders()) {
        LOG_ERROR("SimplePipeline", "Failed to load shaders");
        ghi::destroyBuffer(cameraUBO_);
        ghi::destroyBuffer(lightingUBO_);
        return false;
    }
    
    LOG_INFO("SimplePipeline", "SimplePipeline initialized with shaders");
    
    initialized_ = true;
    return true;
}

void SimplePipeline::shutdown() {
    if (!initialized_) return;
    
    LOG_INFO("SimplePipeline", "Shutting down simple forward pipeline");
    
    if (shader_.isValid()) {
        ghi::destroyShader(shader_);
    }
    
    if (cameraUBO_.isValid()) {
        ghi::destroyBuffer(cameraUBO_);
    }
    
    if (lightingUBO_.isValid()) {
        ghi::destroyBuffer(lightingUBO_);
    }
    
    initialized_ = false;
}

bool SimplePipeline::loadShaders() {
    // Load uber shaders - single source compiled to SPIR-V
    // Metal: SPIR-V -> MSL via SPIRV-Cross
    // Vulkan: SPIR-V directly
    // OpenGL: SPIR-V -> GLSL via SPIRV-Cross
    ghi::ShaderSource source;
    
    switch (backend_) {
        case ghi::Backend::Metal:
            // Metal uses SPIR-V converted to MSL via SPIRV-Cross at runtime
            source.vertexPath = "shaders/uber/uber.vert.spv";
            source.fragmentPath = "shaders/uber/uber.frag.spv";
            LOG_INFO("SimplePipeline", "Loading uber shaders for Metal (SPIR-V -> MSL)");
            break;
            
        case ghi::Backend::Vulkan:
            // Vulkan uses SPIR-V directly
            source.vertexPath = "shaders/uber/uber.vert.spv";
            source.fragmentPath = "shaders/uber/uber.frag.spv";
            LOG_INFO("SimplePipeline", "Loading uber shaders for Vulkan (SPIR-V)");
            break;
            
        case ghi::Backend::OpenGL:
            // OpenGL uses SPIR-V converted to GLSL via SPIRV-Cross
            source.vertexPath = "shaders/uber/uber.vert.spv";
            source.fragmentPath = "shaders/uber/uber.frag.spv";
            LOG_INFO("SimplePipeline", "Loading uber shaders for OpenGL (SPIR-V -> GLSL)");
            break;
            
        default:
            LOG_ERROR("SimplePipeline", "Unsupported backend");
            return false;
    }
    
    shader_ = ghi::createShader(source);
    
    if (!shader_.isValid()) {
        LOG_ERROR("SimplePipeline", "Failed to create shader");
        return false;
    }
    
    LOG_INFO("SimplePipeline", "Loaded uber shader for backend: %s", ghi::getBackendName(backend_));
    
    return true;
}

void SimplePipeline::setCamera(const ral::CameraInfo& camera) {
    currentCamera_ = camera;
    
    // Update matrices
    cameraData_.view = camera.viewMatrix;
    cameraData_.projection = camera.projectionMatrix;
    
    updateCameraUBO();
}

void SimplePipeline::setDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity) {
    lightingData_.sunDirIntensity = glm::vec4(direction, intensity);
    lightingData_.sunColor = glm::vec4(color, 1.0f);
    
    // Sync to combined camera UBO
    cameraData_.sunDirIntensity = lightingData_.sunDirIntensity;
    cameraData_.sunColor = lightingData_.sunColor;
    updateCameraUBO();
    updateLightingUBO();
}

void SimplePipeline::setAmbientLight(const glm::vec3& color, float intensity) {
    lightingData_.ambientColor = glm::vec4(color, intensity);
    
    // Sync to combined camera UBO
    cameraData_.ambientColor = lightingData_.ambientColor;
    updateCameraUBO();
    updateLightingUBO();
}

void SimplePipeline::updateCameraUBO() {
    if (cameraUBO_.isValid()) {
        // Ensure lighting is synced
        cameraData_.sunDirIntensity = lightingData_.sunDirIntensity;
        cameraData_.sunColor = lightingData_.sunColor;
        cameraData_.ambientColor = lightingData_.ambientColor;
        
        ghi::updateBuffer(cameraUBO_, 0, sizeof(CameraData), &cameraData_);
    }
}

void SimplePipeline::updateLightingUBO() {
    if (lightingUBO_.isValid()) {
        ghi::updateBuffer(lightingUBO_, 0, sizeof(LightingData), &lightingData_);
    }
}

void SimplePipeline::beginFrame() {
    if (!initialized_) {
        LOG_ERROR("SimplePipeline", "beginFrame called but not initialized!");
        return;
    }
    
    ghi::beginFrame();
    ghi::beginRenderPass();
    
    // Get actual window dimensions from capabilities
    const ghi::Capabilities& caps = ghi::getCapabilities();
    uint32_t width = caps.maxTextureSize > 0 ? 1280 : 1280;   // TODO: Get from swapchain
    uint32_t height = caps.maxTextureSize > 0 ? 720 : 720;
    
    // Set viewport to match window
    ghi::setViewport(0, 0, width, height);
    ghi::setScissor(0, 0, width, height);
    
    // Bind shader pipeline
    if (shader_.isValid()) {
        ghi::setRenderState({.shader = shader_});
    } else {
        LOG_ERROR("SimplePipeline", "Shader is INVALID!");
    }
    
    // Bind uniform buffers
    ghi::bindUniformBuffer(cameraUBO_, 0, 0);      // Buffer 0: Camera (vertex shader)
}

void SimplePipeline::endFrame() {
    if (!initialized_) return;
    
    ghi::endRenderPass();
    ghi::endFrame();
}

void SimplePipeline::renderMesh(ral::MeshHandle mesh, const glm::mat4& transform, ral::MaterialHandle material) {
    if (!initialized_) return;
    
    // Get mesh buffers from RAL
    ghi::BufferHandle vertexBuffer = ral::getMeshVertexBuffer(mesh);
    ghi::BufferHandle indexBuffer = ral::getMeshIndexBuffer(mesh);
    uint32_t indexCount = ral::getMeshIndexCount(mesh);
    
    if (!vertexBuffer.isValid() || !indexBuffer.isValid() || indexCount == 0) {
        LOG_ERROR("SimplePipeline", "Invalid mesh data: vb=%d, ib=%d, indices=%u",
                  vertexBuffer.isValid(), indexBuffer.isValid(), indexCount);
        return;
    }
    
    // Get material properties from RAL
    glm::vec3 baseColor = ral::getMaterialBaseColor(material);
    float metallic = ral::getMaterialMetallic(material);
    float roughness = ral::getMaterialRoughness(material);

    // Optional debug discriminator (env-var gated)
    static const bool debugPipeline =
        (std::getenv("JUPITER_DEBUG_PIPELINE") != nullptr);
    
    // Build push constant data (model matrix + material properties)
    PushConstantData pushData;
    pushData.model = transform;
    pushData.baseColor = glm::vec4(baseColor, 1.0f);
    // z = pipelineId (0 = Simple), w = debugEnable
    pushData.materialProps = glm::vec4(metallic, roughness, 0.0f, debugPipeline ? 1.0f : 0.0f);
    
    // Set push constants - 96 bytes total (mat4 + 2*vec4)
    ghi::setPushConstants(&pushData, sizeof(PushConstantData), 0);
    
    // Bind vertex and index buffers
    ghi::bindVertexBuffer(vertexBuffer, 0, 0);  // Binding 0, offset 0
    ghi::bindIndexBuffer(indexBuffer, 0);       // Offset 0
    
    // Draw indexed triangles
    ghi::drawIndexed(indexCount, 1, 0, 0, 0);
}

} // namespace rendering
} // namespace jupiter

