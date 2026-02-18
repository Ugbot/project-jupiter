#pragma once

/**
 * @file ral_types.h  
 * @brief RAL Type Definitions
 * 
 * High-level rendering types for mesh, material, light management.
 * Based on Venus RAL + HelloVulkan Scene organization.
 */

#include "rendering/ghi/ghi_types.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace jupiter {
namespace rendering {
namespace ral {

// ============================================================================
// Resource Handles
// ============================================================================

struct MeshHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct MaterialHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

struct LightHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
};

// ============================================================================
// Pipeline Selection
// ============================================================================

enum class Pipeline {
    Simple,      // Forward Lambertian (always available)
    PBR,         // Cook-Torrance + IBL (requires cubemaps)
    Deferred,    // G-Buffer + deferred lighting (requires MRT)
    Clustered,   // Clustered forward (requires compute)
    Voxel        // Voxel rendering (requires compute + indirect)
};

// ============================================================================
// Vertex Formats
// ============================================================================

struct Vertex3D {
    glm::vec3 position;   // 12 bytes, offset 0
    glm::vec3 normal;     // 12 bytes, offset 12
    glm::vec2 texCoord;   // 8 bytes, offset 24
    // Total: 32 bytes (matches GHI pipeline vertex layout)
};

// ============================================================================
// Material
// ============================================================================

enum class MaterialType {
    Simple,     // Color only
    Textured,   // Albedo texture
    PBR         // Full PBR (albedo, normal, metallic, roughness, AO, emissive)
};

struct MaterialInfo {
    MaterialType type = MaterialType::Simple;
    
    // Simple/Textured
    glm::vec3 baseColor = glm::vec3(1.0f);
    float alpha = 1.0f;
    
    // PBR properties
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive = glm::vec3(0.0f);
    float emissiveStrength = 1.0f;
    
    // Textures (GHI handles)
    ghi::TextureHandle albedoTexture;
    ghi::TextureHandle normalTexture;
    ghi::TextureHandle metallicRoughnessTexture;
    ghi::TextureHandle occlusionTexture;
    ghi::TextureHandle emissiveTexture;
};

// ============================================================================
// Mesh
// ============================================================================

struct MeshInfo {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    MaterialHandle material;  // Optional, can be set at render time
    
    // Bounding volume (for culling)
    glm::vec3 boundsMin = glm::vec3(0.0f);
    glm::vec3 boundsMax = glm::vec3(0.0f);
};

// ============================================================================
// Lighting
// ============================================================================

enum class LightType {
    Directional,
    Point,
    Spot
};

struct LightInfo {
    LightType type = LightType::Directional;
    
    glm::vec3 position = glm::vec3(0.0f);       // For point/spot
    glm::vec3 direction = glm::vec3(0, -1, 0);  // For directional/spot
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    
    // Point light attenuation
    float radius = 10.0f;
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.09f;
    float quadraticAttenuation = 0.032f;
    
    // Spot light cone
    float innerConeAngle = 0.4f;  // radians
    float outerConeAngle = 0.6f;  // radians
    
    bool castsShadows = false;
    bool enabled = true;
};

// ============================================================================
// Camera
// ============================================================================

struct CameraInfo {
    glm::vec3 position = glm::vec3(0, 0, 5);
    glm::vec3 target = glm::vec3(0, 0, 0);
    glm::vec3 up = glm::vec3(0, 1, 0);
    
    float fov = 60.0f;  // degrees
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    // Computed matrices (updated by RAL)
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);
};

// ============================================================================
// Render Command
// ============================================================================

struct RenderCommand {
    MeshHandle mesh;
    MaterialHandle material;
    glm::mat4 transform = glm::mat4(1.0f);
    
    // Sorting keys
    float distanceToCamera = 0.0f;  // For transparency sorting
    uint32_t pipelineKey = 0;       // For state batching
    uint32_t materialKey = 0;       // For material batching
};

// ============================================================================
// Render Queue
// ============================================================================

struct RenderQueue {
    std::vector<RenderCommand> opaqueCommands;
    std::vector<RenderCommand> transparentCommands;
    
    void clear() {
        opaqueCommands.clear();
        transparentCommands.clear();
    }
    
    void submit(const RenderCommand& cmd) {
        // TODO: Check material alpha
        opaqueCommands.push_back(cmd);
    }
    
    void sort(const glm::vec3& cameraPosition) {
        // TODO: Sort opaque front-to-back, transparent back-to-front
    }
};

} // namespace ral
} // namespace rendering
} // namespace jupiter

