/**
 * @file ral.cpp
 * @brief Render Abstraction Layer Implementation
 * 
 * High-level rendering API built on GHI.
 * Provides mesh/material/light management and pipeline selection.
 * 
 * Supported pipelines:
 * - Simple: Basic forward rendering with Lambertian lighting
 * - PBR: Physically-based rendering with metallic/roughness workflow
 */

#include "rendering/ral/ral.h"
#include "rendering/ghi/ghi.h"
#include "rendering/pipelines/pipeline_simple.h"
#include "rendering/pipelines/pipeline_pbr.h"
#include "logging/logging.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <unordered_map>
#include <memory>
#include <cmath>

namespace jupiter {
namespace rendering {
namespace ral {

// ============================================================================
// Global State (Minimal)
// ============================================================================

static bool g_initialized = false;
static Pipeline g_activePipeline = Pipeline::Simple;
static std::unique_ptr<SimplePipeline> g_simplePipeline = nullptr;
static std::unique_ptr<PBRPipeline> g_pbrPipeline = nullptr;

static uint32_t g_nextMeshID = 1;
static uint32_t g_nextMaterialID = 1;
static uint32_t g_nextLightID = 1;

// Resource storage
struct MeshData {
    ghi::BufferHandle vertexBuffer;
    ghi::BufferHandle indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    MaterialHandle material;
};

struct MaterialData {
    MaterialType type = MaterialType::Simple;
    ghi::TextureHandle albedoTexture;
    ghi::TextureHandle normalTexture;
    ghi::TextureHandle metallicRoughnessTexture;
    ghi::TextureHandle occlusionTexture;
    ghi::TextureHandle emissiveTexture;
    glm::vec3 baseColor = glm::vec3(1.0f);
    float alpha = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive = glm::vec3(0.0f);
    float emissiveStrength = 1.0f;
};

static std::unordered_map<uint32_t, MeshData> g_meshes;
static std::unordered_map<uint32_t, MaterialData> g_materials;

static CameraInfo g_camera;

// ============================================================================
// Initialization
// ============================================================================

bool initialize() {
    LOG_INFO("RAL", "Initializing Render Abstraction Layer");
    
    if (g_initialized) {
        LOG_WARN("RAL", "Already initialized");
        return true;
    }
    
    ghi::Backend backend = ghi::getActiveBackend();
    
    // Create Simple pipeline (always available)
    g_simplePipeline = std::make_unique<SimplePipeline>();
    if (!g_simplePipeline->initialize(backend)) {
        LOG_ERROR("RAL", "Failed to initialize SimplePipeline");
        g_simplePipeline.reset();
        return false;
    }
    
    // Create PBR pipeline
    g_pbrPipeline = std::make_unique<PBRPipeline>();
    if (!g_pbrPipeline->initialize(backend)) {
        LOG_WARN("RAL", "Failed to initialize PBRPipeline - PBR not available");
        g_pbrPipeline.reset();
    } else {
        LOG_INFO("RAL", "PBR pipeline initialized successfully");
    }
    
    // Set default camera
    g_camera.position = glm::vec3(0, 0, 5);
    g_camera.target = glm::vec3(0, 0, 0);
    g_camera.up = glm::vec3(0, 1, 0);
    g_camera.fov = 60.0f;
    g_camera.aspectRatio = 16.0f / 9.0f;
    g_camera.nearPlane = 0.1f;
    g_camera.farPlane = 1000.0f;
    
    // Update camera matrices
    g_camera.viewMatrix = glm::lookAt(g_camera.position, g_camera.target, g_camera.up);
    g_camera.projectionMatrix = glm::perspective(
        glm::radians(g_camera.fov),
        g_camera.aspectRatio,
        g_camera.nearPlane,
        g_camera.farPlane
    );
    
    // Note: Vulkan Y-flip handled by viewport (negative height)
    
    g_simplePipeline->setCamera(g_camera);
    if (g_pbrPipeline) {
        g_pbrPipeline->setCamera(g_camera);
    }
    
    g_initialized = true;
    LOG_INFO("RAL", "RAL initialized successfully");
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    
    LOG_INFO("RAL", "Shutting down RAL");
    
    // Destroy all meshes
    for (auto& [id, mesh] : g_meshes) {
        if (mesh.vertexBuffer.isValid()) {
            ghi::destroyBuffer(mesh.vertexBuffer);
        }
        if (mesh.indexBuffer.isValid()) {
            ghi::destroyBuffer(mesh.indexBuffer);
        }
    }
    g_meshes.clear();
    
    // Destroy all materials
    for (auto& [id, material] : g_materials) {
        if (material.albedoTexture.isValid()) {
            ghi::destroyTexture(material.albedoTexture);
        }
        if (material.normalTexture.isValid()) {
            ghi::destroyTexture(material.normalTexture);
        }
        if (material.metallicRoughnessTexture.isValid()) {
            ghi::destroyTexture(material.metallicRoughnessTexture);
        }
        if (material.occlusionTexture.isValid()) {
            ghi::destroyTexture(material.occlusionTexture);
        }
        if (material.emissiveTexture.isValid()) {
            ghi::destroyTexture(material.emissiveTexture);
        }
    }
    g_materials.clear();
    
    // Shutdown pipelines
    if (g_pbrPipeline) {
        g_pbrPipeline->shutdown();
        g_pbrPipeline.reset();
    }
    
    if (g_simplePipeline) {
        g_simplePipeline->shutdown();
        g_simplePipeline.reset();
    }
    
    g_initialized = false;
}

bool isInitialized() {
    return g_initialized;
}

// ============================================================================
// Pipeline Selection
// ============================================================================

bool usePipeline(Pipeline pipeline) {
    if (!isPipelineAvailable(pipeline)) {
        LOG_WARN("RAL", "Pipeline not available: %d", (int)pipeline);
        return false;
    }
    
    g_activePipeline = pipeline;
    LOG_INFO("RAL", "Switched to pipeline: %s", 
             pipeline == Pipeline::Simple ? "Simple" : 
             pipeline == Pipeline::PBR ? "PBR" : "Unknown");
    return true;
}

Pipeline getActivePipeline() {
    return g_activePipeline;
}

bool isPipelineAvailable(Pipeline pipeline) {
    switch (pipeline) {
        case Pipeline::Simple:
            return g_simplePipeline != nullptr && g_simplePipeline->isInitialized();
        case Pipeline::PBR:
            return g_pbrPipeline != nullptr && g_pbrPipeline->isInitialized();
        case Pipeline::Deferred:
        case Pipeline::Clustered:
        case Pipeline::Voxel:
            return false;  // Not yet implemented
        default:
            return false;
    }
}

// ============================================================================
// Mesh Management
// ============================================================================

MeshHandle createMesh(const MeshInfo& info) {
    if (info.vertices.empty()) {
        LOG_ERROR("RAL", "Cannot create mesh with no vertices");
        return MeshHandle{};
    }
    
    MeshData meshData;
    
    // Create vertex buffer
    meshData.vertexBuffer = ghi::createBuffer({
        .type = ghi::BufferType::Vertex,
        .usage = ghi::BufferUsage::Static,
        .size = info.vertices.size() * sizeof(Vertex3D),
        .data = info.vertices.data()
    });
    
    if (!meshData.vertexBuffer.isValid()) {
        LOG_ERROR("RAL", "Failed to create vertex buffer");
        return MeshHandle{};
    }
    
    // Create index buffer if we have indices
    // Convert to uint16_t for GPU efficiency (matching backend expectation)
    if (!info.indices.empty()) {
        std::vector<uint16_t> indices16(info.indices.size());
        for (size_t i = 0; i < info.indices.size(); i++) {
            indices16[i] = static_cast<uint16_t>(info.indices[i]);
        }
        
        meshData.indexBuffer = ghi::createBuffer({
            .type = ghi::BufferType::Index,
            .usage = ghi::BufferUsage::Static,
            .size = indices16.size() * sizeof(uint16_t),
            .data = indices16.data()
        });
        
        if (!meshData.indexBuffer.isValid()) {
            LOG_ERROR("RAL", "Failed to create index buffer");
            ghi::destroyBuffer(meshData.vertexBuffer);
            return MeshHandle{};
        }
        
        meshData.indexCount = static_cast<uint32_t>(info.indices.size());
    }
    
    meshData.vertexCount = static_cast<uint32_t>(info.vertices.size());
    meshData.material = info.material;
    
    MeshHandle handle;
    handle.id = g_nextMeshID++;
    g_meshes[handle.id] = meshData;
    
    LOG_INFO("RAL", "Created mesh: id=%u, vertices=%u, indices=%u", 
             handle.id, meshData.vertexCount, meshData.indexCount);
    
    return handle;
}

void destroyMesh(MeshHandle handle) {
    auto it = g_meshes.find(handle.id);
    if (it != g_meshes.end()) {
        if (it->second.vertexBuffer.isValid()) {
            ghi::destroyBuffer(it->second.vertexBuffer);
        }
        if (it->second.indexBuffer.isValid()) {
            ghi::destroyBuffer(it->second.indexBuffer);
        }
        g_meshes.erase(it);
    }
}

ghi::BufferHandle getMeshVertexBuffer(MeshHandle handle) {
    auto it = g_meshes.find(handle.id);
    if (it != g_meshes.end()) {
        return it->second.vertexBuffer;
    }
    return {};
}

ghi::BufferHandle getMeshIndexBuffer(MeshHandle handle) {
    auto it = g_meshes.find(handle.id);
    if (it != g_meshes.end()) {
        return it->second.indexBuffer;
    }
    return {};
}

uint32_t getMeshIndexCount(MeshHandle handle) {
    auto it = g_meshes.find(handle.id);
    if (it != g_meshes.end()) {
        return it->second.indexCount;
    }
    return 0;
}

uint32_t getMeshVertexCount(MeshHandle handle) {
    auto it = g_meshes.find(handle.id);
    if (it != g_meshes.end()) {
        return it->second.vertexCount;
    }
    return 0;
}

glm::vec3 getMaterialBaseColor(MaterialHandle handle) {
    auto it = g_materials.find(handle.id);
    if (it != g_materials.end()) {
        return it->second.baseColor;
    }
    return glm::vec3(1.0f);  // White default
}

float getMaterialMetallic(MaterialHandle handle) {
    auto it = g_materials.find(handle.id);
    if (it != g_materials.end()) {
        return it->second.metallic;
    }
    return 0.0f;  // Default: non-metallic
}

float getMaterialRoughness(MaterialHandle handle) {
    auto it = g_materials.find(handle.id);
    if (it != g_materials.end()) {
        return it->second.roughness;
    }
    return 0.5f;  // Default: medium roughness
}

glm::vec3 getMaterialEmissive(MaterialHandle handle) {
    auto it = g_materials.find(handle.id);
    if (it != g_materials.end()) {
        return it->second.emissive * it->second.emissiveStrength;
    }
    return glm::vec3(0.0f);  // Default: no emission
}

bool isMaterialPBR(MaterialHandle handle) {
    auto it = g_materials.find(handle.id);
    if (it != g_materials.end()) {
        return it->second.type == MaterialType::PBR;
    }
    return false;
}

// ============================================================================
// Material Management
// ============================================================================

MaterialHandle createMaterial(const MaterialInfo& info) {
    MaterialData matData;
    matData.albedoTexture = info.albedoTexture;
    matData.baseColor = info.baseColor;
    matData.metallic = info.metallic;
    matData.roughness = info.roughness;
    
    MaterialHandle handle;
    handle.id = g_nextMaterialID++;
    g_materials[handle.id] = matData;
    
    LOG_INFO("RAL", "Created material: id=%u", handle.id);
    return handle;
}

MaterialHandle createSimpleMaterial(glm::vec3 color) {
    MaterialInfo info;
    info.type = MaterialType::Simple;
    info.baseColor = color;
    return createMaterial(info);
}

void destroyMaterial(MaterialHandle handle) {
    auto it = g_materials.find(handle.id);
    if (it != g_materials.end()) {
        if (it->second.albedoTexture.isValid()) {
            ghi::destroyTexture(it->second.albedoTexture);
        }
        g_materials.erase(it);
    }
}

// ============================================================================
// Camera
// ============================================================================

void setCamera(const CameraInfo& camera) {
    g_camera = camera;
    
    // Update matrices
    g_camera.viewMatrix = glm::lookAt(camera.position, camera.target, camera.up);
    g_camera.projectionMatrix = glm::perspective(
        glm::radians(camera.fov),
        camera.aspectRatio,
        camera.nearPlane,
        camera.farPlane
    );
    
    // Note: Vulkan Y-flip is now handled by viewport (negative height)
    // This preserves winding order and simplifies cross-platform rendering
    
    if (g_simplePipeline) {
        g_simplePipeline->setCamera(g_camera);
    }
    if (g_pbrPipeline) {
        g_pbrPipeline->setCamera(g_camera);
    }
}

const CameraInfo& getCamera() {
    return g_camera;
}

// ============================================================================
// Lighting
// ============================================================================

void setAmbientLight(glm::vec3 color, float intensity) {
    if (g_simplePipeline) {
        g_simplePipeline->setAmbientLight(color, intensity);
    }
    if (g_pbrPipeline) {
        g_pbrPipeline->setAmbientLight(color, intensity);
    }
}

LightHandle createDirectionalLight(glm::vec3 direction, glm::vec3 color, float intensity) {
    if (g_simplePipeline) {
        g_simplePipeline->setDirectionalLight(direction, color, intensity);
    }
    if (g_pbrPipeline) {
        g_pbrPipeline->setDirectionalLight(direction, color, intensity);
    }
    
    LightHandle handle;
    handle.id = g_nextLightID++;
    return handle;
}

// Stubs for remaining light functions (need proper implementation)
LightHandle createLight(const LightInfo& info) {
    switch (info.type) {
        case LightType::Directional:
            return createDirectionalLight(info.direction, info.color, info.intensity);
        case LightType::Point:
            return createPointLight(info.position, info.color, info.radius, info.intensity);
        case LightType::Spot:
            return createSpotLight(info.position, info.direction, info.color, 
                                   info.innerConeAngle, info.outerConeAngle, info.intensity);
    }
    return LightHandle{};
}

void destroyLight(LightHandle handle) {
    // TODO: Implement proper light destruction
}

void updateLight(LightHandle handle, const LightInfo& info) {
    // TODO: Implement light update
}

LightHandle createPointLight(glm::vec3 position, glm::vec3 color, float radius, float intensity) {
    if (g_pbrPipeline) {
        g_pbrPipeline->addPointLight(position, color, intensity, radius);
    }
    
    LightHandle handle;
    handle.id = g_nextLightID++;
    return handle;
}

LightHandle createSpotLight(glm::vec3 position, glm::vec3 direction, glm::vec3 color, 
                           float innerAngle, float outerAngle, float intensity) {
    if (g_pbrPipeline) {
        g_pbrPipeline->addSpotLight(position, direction, color, intensity, innerAngle, outerAngle, 50.0f);
    }
    
    LightHandle handle;
    handle.id = g_nextLightID++;
    return handle;
}

// ============================================================================
// Rendering
// ============================================================================

void beginFrame() {
    if (!g_initialized) return;
    
    switch (g_activePipeline) {
        case Pipeline::PBR:
            if (g_pbrPipeline && g_pbrPipeline->hasValidShader()) {
                // PBR pipeline handles beginFrame/beginRenderPass internally
                g_pbrPipeline->beginFrame();
            } else if (g_simplePipeline) {
                // Fallback to simple pipeline
                g_simplePipeline->beginFrame();
            }
            break;
        case Pipeline::Simple:
        default:
            if (g_simplePipeline) {
                g_simplePipeline->beginFrame();
            }
            break;
    }
}

void endFrame() {
    if (!g_initialized) return;
    
    switch (g_activePipeline) {
        case Pipeline::PBR:
            if (g_pbrPipeline && g_pbrPipeline->hasValidShader()) {
                // PBR pipeline handles endRenderPass/endFrame internally
                g_pbrPipeline->endFrame();
            } else if (g_simplePipeline) {
                // Fallback to simple pipeline
                g_simplePipeline->endFrame();
            }
            break;
        case Pipeline::Simple:
        default:
            if (g_simplePipeline) {
                g_simplePipeline->endFrame();
            }
            break;
    }
}

void renderMesh(MeshHandle mesh, const glm::mat4& transform, MaterialHandle material) {
    if (!g_initialized) return;
    
    auto it = g_meshes.find(mesh.id);
    if (it == g_meshes.end()) {
        LOG_ERROR("RAL", "Invalid mesh handle: %u", mesh.id);
        return;
    }
    
    switch (g_activePipeline) {
        case Pipeline::PBR:
            if (g_pbrPipeline && g_pbrPipeline->hasValidShader()) {
                g_pbrPipeline->renderMesh(mesh, transform, material);
                
                // Also need to bind and draw the mesh
                ghi::bindVertexBuffer(it->second.vertexBuffer, 0);
                ghi::bindIndexBuffer(it->second.indexBuffer, 0);
                ghi::drawIndexed(it->second.indexCount, 1, 0, 0, 0);
            } else if (g_simplePipeline) {
                // Fallback to simple pipeline if PBR shader is invalid
                g_simplePipeline->renderMesh(mesh, transform, material);
            }
            break;
        case Pipeline::Simple:
        default:
            if (g_simplePipeline) {
                g_simplePipeline->renderMesh(mesh, transform, material);
            }
            break;
    }
}


// ============================================================================
// Primitive Mesh Generators
// ============================================================================

MeshHandle createCube(float size) {
    float half = size * 0.5f;
    
    // 24 vertices (4 per face for proper normals and UVs)
    std::vector<Vertex3D> vertices = {
        // Front face (z = +half)
        {{ -half, -half, half }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }},
        {{  half, -half, half }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }},
        {{  half,  half, half }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }},
        {{ -half,  half, half }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }},
        
        // Back face (z = -half)
        {{  half, -half, -half }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f }},
        {{ -half, -half, -half }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f }},
        {{ -half,  half, -half }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f }},
        {{  half,  half, -half }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f }},
        
        // Top face (y = +half)
        {{ -half, half, half }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }},
        {{  half, half, half }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }},
        {{  half, half, -half }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }},
        {{ -half, half, -half }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }},
        
        // Bottom face (y = -half)
        {{ -half, -half, -half }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }},
        {{  half, -half, -half }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }},
        {{  half, -half, half }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f }},
        {{ -half, -half, half }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f }},
        
        // Right face (x = +half)
        {{ half, -half, half }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }},
        {{ half, -half, -half }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
        {{ half,  half, -half }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }},
        {{ half,  half, half }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f }},
        
        // Left face (x = -half)
        {{ -half, -half, -half }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f }},
        {{ -half, -half, half }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f }},
        {{ -half,  half, half }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }},
        {{ -half,  half, -half }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f }},
    };
    
    std::vector<uint32_t> indices = {
        0, 1, 2,  2, 3, 0,   // Front
        4, 5, 6,  6, 7, 4,   // Back
        8, 9, 10, 10, 11, 8,  // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        20, 21, 22, 22, 23, 20  // Left
    };
    
    MeshInfo info;
    info.vertices = vertices;
    info.indices = indices;
    
    MeshHandle handle = createMesh(info);
    LOG_INFO("RAL", "Created cube: size=%.2f, handle=%u", size, handle.id);
    return handle;
}

MeshHandle createSphere(float radius, uint32_t segments) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    
    uint32_t rings = segments / 2;
    
    // Generate vertices
    for (uint32_t ring = 0; ring <= rings; ring++) {
        float phi = (float)ring / rings * glm::pi<float>();
        float y = radius * std::cos(phi);
        float ringRadius = radius * std::sin(phi);
        
        for (uint32_t seg = 0; seg <= segments; seg++) {
            float theta = (float)seg / segments * 2.0f * glm::pi<float>();
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);
            
            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos);
            glm::vec2 uv((float)seg / segments, (float)ring / rings);
            
            vertices.push_back({ pos, normal, uv });
        }
    }
    
    // Generate indices
    for (uint32_t ring = 0; ring < rings; ring++) {
        for (uint32_t seg = 0; seg < segments; seg++) {
            uint32_t curr = ring * (segments + 1) + seg;
            uint32_t next = curr + segments + 1;
            
            indices.push_back(curr);
            indices.push_back(next);
            indices.push_back(curr + 1);
            
            indices.push_back(curr + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    MeshInfo info;
    info.vertices = vertices;
    info.indices = indices;
    
    MeshHandle handle = createMesh(info);
    LOG_INFO("RAL", "Created sphere: radius=%.2f, segments=%u, handle=%u", radius, segments, handle.id);
    return handle;
}

MeshHandle createPlane(float width, float height, uint32_t segmentsX, uint32_t segmentsZ) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    
    float halfW = width * 0.5f;
    float halfH = height * 0.5f;
    
    // Generate vertices
    for (uint32_t z = 0; z <= segmentsZ; z++) {
        float tz = (float)z / segmentsZ;
        float pz = -halfH + tz * height;
        
        for (uint32_t x = 0; x <= segmentsX; x++) {
            float tx = (float)x / segmentsX;
            float px = -halfW + tx * width;
            
            vertices.push_back({
                { px, 0.0f, pz },
                { 0.0f, 1.0f, 0.0f },
                { tx, tz }
            });
        }
    }
    
    // Generate indices
    for (uint32_t z = 0; z < segmentsZ; z++) {
        for (uint32_t x = 0; x < segmentsX; x++) {
            uint32_t curr = z * (segmentsX + 1) + x;
            uint32_t next = curr + segmentsX + 1;
            
            indices.push_back(curr);
            indices.push_back(next);
            indices.push_back(curr + 1);
            
            indices.push_back(curr + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    MeshInfo info;
    info.vertices = vertices;
    info.indices = indices;
    
    MeshHandle handle = createMesh(info);
    LOG_INFO("RAL", "Created plane: size=%.2fx%.2f, segments=%ux%u, handle=%u", 
             width, height, segmentsX, segmentsZ, handle.id);
    return handle;
}

MeshHandle createCylinder(float radius, float height, uint32_t segments) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    
    float halfH = height * 0.5f;
    
    // Side vertices (two rings)
    for (uint32_t i = 0; i <= segments; i++) {
        float theta = (float)i / segments * 2.0f * glm::pi<float>();
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
        float u = (float)i / segments;
        
        // Top ring
        vertices.push_back({ { x, halfH, z }, normal, { u, 1.0f } });
        // Bottom ring
        vertices.push_back({ { x, -halfH, z }, normal, { u, 0.0f } });
    }
    
    uint32_t sideStart = 0;
    uint32_t sideVerts = (segments + 1) * 2;
    
    // Side indices
    for (uint32_t i = 0; i < segments; i++) {
        uint32_t top1 = sideStart + i * 2;
        uint32_t bot1 = top1 + 1;
        uint32_t top2 = top1 + 2;
        uint32_t bot2 = top1 + 3;
        
        indices.push_back(top1);
        indices.push_back(bot1);
        indices.push_back(top2);
        
        indices.push_back(top2);
        indices.push_back(bot1);
        indices.push_back(bot2);
    }
    
    // Top cap center
    uint32_t topCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ { 0.0f, halfH, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.5f, 0.5f } });
    
    // Top cap ring
    for (uint32_t i = 0; i <= segments; i++) {
        float theta = (float)i / segments * 2.0f * glm::pi<float>();
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        float u = 0.5f + 0.5f * std::cos(theta);
        float v = 0.5f + 0.5f * std::sin(theta);
        vertices.push_back({ { x, halfH, z }, { 0.0f, 1.0f, 0.0f }, { u, v } });
    }
    
    // Top cap indices
    for (uint32_t i = 0; i < segments; i++) {
        indices.push_back(topCenter);
        indices.push_back(topCenter + 1 + i);
        indices.push_back(topCenter + 2 + i);
    }
    
    // Bottom cap center
    uint32_t botCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ { 0.0f, -halfH, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.5f, 0.5f } });
    
    // Bottom cap ring
    for (uint32_t i = 0; i <= segments; i++) {
        float theta = (float)i / segments * 2.0f * glm::pi<float>();
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        float u = 0.5f + 0.5f * std::cos(theta);
        float v = 0.5f + 0.5f * std::sin(theta);
        vertices.push_back({ { x, -halfH, z }, { 0.0f, -1.0f, 0.0f }, { u, v } });
    }
    
    // Bottom cap indices (reversed winding)
    for (uint32_t i = 0; i < segments; i++) {
        indices.push_back(botCenter);
        indices.push_back(botCenter + 2 + i);
        indices.push_back(botCenter + 1 + i);
    }
    
    MeshInfo info;
    info.vertices = vertices;
    info.indices = indices;
    
    MeshHandle handle = createMesh(info);
    LOG_INFO("RAL", "Created cylinder: radius=%.2f, height=%.2f, segments=%u, handle=%u",
             radius, height, segments, handle.id);
    return handle;
}

MeshHandle createCapsule(float radius, float height, uint32_t segments) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    
    float halfH = (height - 2.0f * radius) * 0.5f;
    if (halfH < 0.0f) halfH = 0.0f;
    
    uint32_t rings = segments / 2;
    
    // Top hemisphere
    for (uint32_t ring = 0; ring <= rings / 2; ring++) {
        float phi = (float)ring / rings * glm::pi<float>();
        float y = radius * std::cos(phi) + halfH;
        float ringRadius = radius * std::sin(phi);
        
        for (uint32_t seg = 0; seg <= segments; seg++) {
            float theta = (float)seg / segments * 2.0f * glm::pi<float>();
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);
            
            glm::vec3 normal = glm::normalize(glm::vec3(x, radius * std::cos(phi), z));
            float u = (float)seg / segments;
            float v = 1.0f - (float)ring / rings * 0.5f;
            
            vertices.push_back({ { x, y, z }, normal, { u, v } });
        }
    }
    
    // Cylinder section
    for (int i = 0; i <= 1; i++) {
        float y = halfH - i * 2.0f * halfH;
        for (uint32_t seg = 0; seg <= segments; seg++) {
            float theta = (float)seg / segments * 2.0f * glm::pi<float>();
            float x = radius * std::cos(theta);
            float z = radius * std::sin(theta);
            
            glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
            float u = (float)seg / segments;
            float v = 0.5f - i * 0.0f;
            
            vertices.push_back({ { x, y, z }, normal, { u, v } });
        }
    }
    
    // Bottom hemisphere
    for (uint32_t ring = rings / 2; ring <= rings; ring++) {
        float phi = (float)ring / rings * glm::pi<float>();
        float y = radius * std::cos(phi) - halfH;
        float ringRadius = radius * std::sin(phi);
        
        for (uint32_t seg = 0; seg <= segments; seg++) {
            float theta = (float)seg / segments * 2.0f * glm::pi<float>();
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);
            
            glm::vec3 normal = glm::normalize(glm::vec3(x, radius * std::cos(phi), z));
            float u = (float)seg / segments;
            float v = (float)ring / rings * 0.5f;
            
            vertices.push_back({ { x, y, z }, normal, { u, v } });
        }
    }
    
    // Generate indices for all rings
    uint32_t numRings = rings / 2 + 1 + 2 + rings / 2 + 1;
    for (uint32_t ring = 0; ring < numRings - 1; ring++) {
        for (uint32_t seg = 0; seg < segments; seg++) {
            uint32_t curr = ring * (segments + 1) + seg;
            uint32_t next = curr + segments + 1;
            
            indices.push_back(curr);
            indices.push_back(next);
            indices.push_back(curr + 1);
            
            indices.push_back(curr + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }
    
    MeshInfo info;
    info.vertices = vertices;
    info.indices = indices;
    
    MeshHandle handle = createMesh(info);
    LOG_INFO("RAL", "Created capsule: radius=%.2f, height=%.2f, segments=%u, handle=%u",
             radius, height, segments, handle.id);
    return handle;
}

MaterialHandle createTexturedMaterial(ghi::TextureHandle albedo) {
    MaterialInfo info;
    info.type = MaterialType::Textured;
    info.albedoTexture = albedo;
    info.baseColor = glm::vec3(1.0f);
    return createMaterial(info);
}

MaterialHandle createPBRMaterial(glm::vec3 albedo, float metallic, float roughness) {
    MaterialData matData;
    matData.type = MaterialType::PBR;
    matData.baseColor = albedo;
    matData.alpha = 1.0f;
    matData.metallic = metallic;
    matData.roughness = roughness;
    matData.ao = 1.0f;
    matData.emissive = glm::vec3(0.0f);
    matData.emissiveStrength = 1.0f;
    
    MaterialHandle handle;
    handle.id = g_nextMaterialID++;
    g_materials[handle.id] = matData;
    
    LOG_INFO("RAL", "Created PBR material: id=%u, metallic=%.2f, roughness=%.2f", 
             handle.id, metallic, roughness);
    return handle;
}

MaterialHandle createPBRMaterialTextured(
    ghi::TextureHandle albedo,
    ghi::TextureHandle normal,
    ghi::TextureHandle metallicRoughness,
    ghi::TextureHandle occlusion,
    ghi::TextureHandle emissive
) {
    MaterialData matData;
    matData.type = MaterialType::PBR;
    matData.baseColor = glm::vec3(1.0f);
    matData.alpha = 1.0f;
    matData.metallic = 1.0f;  // Will be overridden by texture
    matData.roughness = 1.0f; // Will be overridden by texture
    matData.ao = 1.0f;
    matData.emissive = glm::vec3(0.0f);
    matData.emissiveStrength = 1.0f;
    matData.albedoTexture = albedo;
    matData.normalTexture = normal;
    matData.metallicRoughnessTexture = metallicRoughness;
    matData.occlusionTexture = occlusion;
    matData.emissiveTexture = emissive;
    
    MaterialHandle handle;
    handle.id = g_nextMaterialID++;
    g_materials[handle.id] = matData;
    
    LOG_INFO("RAL", "Created PBR textured material: id=%u", handle.id);
    return handle;
}

bool loadEnvironment(const char* hdrPath) {
    if (g_pbrPipeline) {
        return g_pbrPipeline->loadEnvironment(hdrPath);
    }
    LOG_WARN("RAL", "Cannot load environment - PBR pipeline not available");
    return false;
}

void enableShadows(bool enable) {
    // TODO: Implement shadow mapping
    LOG_WARN("RAL", "Shadow mapping not yet implemented");
}

void enableSSAO(bool enable) {
    // TODO: Implement SSAO
    LOG_WARN("RAL", "SSAO not yet implemented");
}

void enableBloom(bool enable) {
    // TODO: Implement bloom
    LOG_WARN("RAL", "Bloom not yet implemented");
}

void enableHDR(bool enable) {
    if (g_pbrPipeline) {
        g_pbrPipeline->setIBLEnabled(enable);
    }
}

void setExposure(float exposure) {
    if (g_pbrPipeline) {
        g_pbrPipeline->setExposure(exposure);
    }
}

const Statistics& getStatistics() { static Statistics s; return s; }
void resetStatistics() {}
void setWireframeMode(bool enabled) {}
void setClearColor(glm::vec4 color) {}
void flushRenderQueue() {}

} // namespace ral
} // namespace rendering
} // namespace jupiter

