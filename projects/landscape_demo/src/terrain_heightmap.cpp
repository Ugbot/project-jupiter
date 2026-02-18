#include "terrain_heightmap.h"
#include "rendering/default_textures.h"
#include "logging/logging.h"
#include <glm/gtc/noise.hpp>
#include <cmath>

using namespace jupiter;
using namespace jupiter::rendering;

namespace landscape {

float TerrainHeightmap::getNoiseHeight(float worldX, float worldZ) const {
    float h = 0.0f;
    h += glm::perlin(glm::vec2(worldX * 0.005f, worldZ * 0.005f)) * 20.0f;  // large hills
    h += glm::perlin(glm::vec2(worldX * 0.02f, worldZ * 0.02f)) * 5.0f;     // medium
    h += glm::perlin(glm::vec2(worldX * 0.1f, worldZ * 0.1f)) * 1.0f;       // detail
    return h * config_.heightScale;
}

bool TerrainHeightmap::generate(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool commandPool, VkQueue graphicsQueue,
                                 const TerrainConfig& config) {
    config_ = config;
    
    LOG_INFO("Terrain", "Generating heightmap terrain (%.0fm, %u segments)", 
             config_.size, config_.segments);
    
    // Generate base plane mesh
    meshData_ = primitives::createPlane(config_.size, config_.size, 
                                        static_cast<int>(config_.segments));
    
    LOG_INFO("Terrain", "Displacing vertices (%zu vertices)", meshData_.vertices.size());
    
    // Displace Y using multi-octave noise
    for (auto& v : meshData_.vertices) {
        float wx = v.position.x;  // world X
        float wz = v.position.z;  // world Z
        v.position.y = getNoiseHeight(wx, wz);
    }
    
    // Recompute normals for lighting (recalculate based on neighbor heights)
    // For now, use the original normals - proper normal recalculation would
    // need to be added to the primitives helper
    LOG_INFO("Terrain", "Note: Normal recalculation not yet implemented for displaced meshes");
    
    LOG_INFO("Terrain", "Creating GPU mesh (%zu indices)", meshData_.indices.size());
    
    // Convert indices from uint16_t to uint32_t for VulkanMesh
    std::vector<uint32_t> indices32(meshData_.indices.begin(), meshData_.indices.end());
    
    // Create GPU mesh using raw vertex data
    mesh_ = std::make_unique<VulkanMesh>();
    if (!mesh_->create(device, allocator, 
                       meshData_.vertices.data(),
                       static_cast<uint32_t>(meshData_.vertices.size()),
                       sizeof(primitives::Vertex),
                       indices32)) {
        LOG_ERROR("Terrain", "Failed to create GPU mesh");
        return false;
    }
    
    // Generate heightmap texture (R32F, same noise)
    LOG_INFO("Terrain", "Generating heightmap texture (%ux%u)", 
             config_.textureRes, config_.textureRes);
    
    heightData_.resize(config_.textureRes * config_.textureRes);
    
    for (uint32_t y = 0; y < config_.textureRes; ++y) {
        for (uint32_t x = 0; x < config_.textureRes; ++x) {
            // UV to world coords (terrain centered at origin)
            float u = static_cast<float>(x) / static_cast<float>(config_.textureRes - 1);
            float v = static_cast<float>(y) / static_cast<float>(config_.textureRes - 1);
            
            float worldX = (u - 0.5f) * config_.size;
            float worldZ = (v - 0.5f) * config_.size;
            
            float height = getNoiseHeight(worldX, worldZ);
            heightData_[y * config_.textureRes + x] = height;
        }
    }
    
    // Upload heightmap texture (no mipmaps)
    if (!heightmapTexture_.create(device, allocator, commandPool, graphicsQueue,
                                   heightData_.data(), config_.textureRes, config_.textureRes,
                                   VK_FORMAT_R32_SFLOAT, false)) {
        LOG_ERROR("Terrain", "Failed to create heightmap texture");
        return false;
    }
    
    // Create sampler (linear filtering, clamp to edge)
    if (!heightmapTexture_.createSampler(device, VK_FILTER_LINEAR,
                                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                         false, 1.0f)) {
        LOG_ERROR("Terrain", "Failed to create heightmap sampler");
        return false;
    }
    
    LOG_INFO("Terrain", "Terrain generation complete");
    return true;
}


RenderableHandle TerrainHeightmap::addToScene(SceneManager* sceneManager,
                                               MaterialSystem* materialSystem,
                                               VmaAllocator allocator) {
    if (!mesh_ || !sceneManager || !materialSystem) {
        LOG_ERROR("Terrain", "Cannot add to scene: invalid state");
        return RenderableHandle{};
    }
    
    // Create simple PBR material (greenish terrain)
    float terrainColor[3] = {0.4f, 0.5f, 0.3f};  // green-brown
    Material* material = materialSystem->createSimpleMaterial(
        allocator,
        DefaultTextures::get().getWhiteTexture(),   // albedo
        DefaultTextures::get().getNormalTexture(),  // normal
        DefaultTextures::get().getWhiteTexture(),   // metallic/roughness
        DefaultTextures::get().getWhiteTexture(),   // occlusion
        DefaultTextures::get().getBlackTexture(),   // emissive
        terrainColor
    );
    
    if (!material) {
        LOG_ERROR("Terrain", "Failed to create terrain material");
        return RenderableHandle{};
    }
    
    // Create renderable
    Renderable renderable;
    renderable.mesh = mesh_.get();
    renderable.material = material;
    renderable.transform = glm::mat4(1.0f);  // identity (centered at origin)
    
    RenderableHandle handle = sceneManager->addRenderable(renderable);
    LOG_INFO("Terrain", "Added terrain to scene");
    return handle;
}

float TerrainHeightmap::sampleHeight(float worldX, float worldZ) const {
    // UV from world coords
    float u = (worldX / config_.size) + 0.5f;
    float v = (worldZ / config_.size) + 0.5f;
    
    // Clamp to texture bounds
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return 0.0f;
    }
    
    // Bilinear sample from heightData_
    float fx = u * static_cast<float>(config_.textureRes - 1);
    float fy = v * static_cast<float>(config_.textureRes - 1);
    
    uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
    uint32_t x1 = std::min(x0 + 1, config_.textureRes - 1);
    uint32_t y1 = std::min(y0 + 1, config_.textureRes - 1);
    
    float tx = fx - std::floor(fx);
    float ty = fy - std::floor(fy);
    
    float h00 = heightData_[y0 * config_.textureRes + x0];
    float h10 = heightData_[y0 * config_.textureRes + x1];
    float h01 = heightData_[y1 * config_.textureRes + x0];
    float h11 = heightData_[y1 * config_.textureRes + x1];
    
    float h0 = h00 * (1.0f - tx) + h10 * tx;
    float h1 = h01 * (1.0f - tx) + h11 * tx;
    
    return h0 * (1.0f - ty) + h1 * ty;
}

} // namespace landscape

