/**
 * @file grass_system.cpp
 * @brief GHI-based grass system implementation
 */

#include "rendering/ral/grass_system.h"
#include "logging/logging.h"

namespace jupiter {
namespace rendering {
namespace ral {

/**
 * @brief Grass instance data (matches compute shader output)
 */
struct GrassInstance {
    glm::vec4 posHeight;    // xyz = world pos, w = blade height
    glm::vec4 normalSeed;   // xyz = world normal, w = random seed
    glm::vec4 bendFlatten;  // xy = trail bend dir, z = bend amount, w = flatten amount
};

/**
 * @brief Indirect draw command structure
 */
struct DrawIndirectCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};

GrassSystem::~GrassSystem() {
    shutdown();
}

void GrassSystem::shutdown() {
    if (!initialized_) return;

    LOG_INFO("GrassSystem", "Shutting down grass system");

    // Destroy buffers
    if (instanceBuffer_.isValid()) ghi::destroyBuffer(instanceBuffer_);
    if (indirectBuffer_.isValid()) ghi::destroyBuffer(indirectBuffer_);
    if (counterBuffer_.isValid()) ghi::destroyBuffer(counterBuffer_);

    // Destroy shaders
    if (generateShader_.isValid()) ghi::destroyShader(generateShader_);
    if (grassShader_.isValid()) ghi::destroyShader(grassShader_);

    initialized_ = false;
}

bool GrassSystem::initialize() {
    LOG_INFO("GrassSystem", "Initializing GHI grass system");

    // Check compute shader support
    if (!ghi::hasComputeShaders()) {
        LOG_ERROR("GrassSystem", "Compute shaders not supported - grass system requires compute");
        return false;
    }

    // Create GPU buffers
    if (!createBuffers()) {
        LOG_ERROR("GrassSystem", "Failed to create grass buffers");
        return false;
    }

    // Create shaders
    if (!createShaders()) {
        LOG_ERROR("GrassSystem", "Failed to create grass shaders");
        return false;
    }

    initialized_ = true;
    LOG_INFO("GrassSystem", "Grass system initialized successfully");
    return true;
}

bool GrassSystem::createBuffers() {
    // Instance buffer (storage buffer for compute output)
    ghi::BufferCreateInfo instanceInfo;
    instanceInfo.type = ghi::BufferType::Storage;
    instanceInfo.usage = ghi::BufferUsage::Dynamic;
    instanceInfo.size = sizeof(GrassInstance) * MAX_GRASS_INSTANCES;

    instanceBuffer_ = ghi::createBuffer(instanceInfo);
    if (!instanceBuffer_.isValid()) {
        LOG_ERROR("GrassSystem", "Failed to create instance buffer");
        return false;
    }
    LOG_INFO("GrassSystem", "Created instance buffer (%zu MB)", 
             instanceInfo.size / (1024 * 1024));

    // Indirect draw command buffer
    ghi::BufferCreateInfo indirectInfo;
    indirectInfo.type = ghi::BufferType::Indirect;
    indirectInfo.usage = ghi::BufferUsage::Dynamic;
    indirectInfo.size = sizeof(DrawIndirectCommand);

    indirectBuffer_ = ghi::createBuffer(indirectInfo);
    if (!indirectBuffer_.isValid()) {
        LOG_ERROR("GrassSystem", "Failed to create indirect buffer");
        return false;
    }

    // Counter buffer (atomic counter for instance count)
    ghi::BufferCreateInfo counterInfo;
    counterInfo.type = ghi::BufferType::Storage;
    counterInfo.usage = ghi::BufferUsage::Dynamic;
    counterInfo.size = sizeof(uint32_t);

    counterBuffer_ = ghi::createBuffer(counterInfo);
    if (!counterBuffer_.isValid()) {
        LOG_ERROR("GrassSystem", "Failed to create counter buffer");
        return false;
    }

    LOG_INFO("GrassSystem", "Created grass GPU buffers");
    return true;
}

bool GrassSystem::createShaders() {
    // Load grass generation compute shader
    ghi::ShaderSource genSource;
    genSource.computePath = "rendering/shaders/compute/grass_generate.comp.spv";

    generateShader_ = ghi::createComputeShader(genSource);
    if (!generateShader_.isValid()) {
        LOG_WARN("GrassSystem", "Grass generate shader not found - using stub");
        // Not a fatal error - shader may not exist yet
    }

    // Load grass rendering shader
    ghi::ShaderSource grassSource;
    grassSource.vertexPath = "rendering/shaders/foliage/grass.vert.spv";
    grassSource.fragmentPath = "rendering/shaders/foliage/grass.frag.spv";

    grassShader_ = ghi::createShader(grassSource);
    if (!grassShader_.isValid()) {
        LOG_WARN("GrassSystem", "Grass render shader not found - using stub");
        // Not a fatal error - shader may not exist yet
    }

    LOG_INFO("GrassSystem", "Created grass shaders (compute: %s, render: %s)",
             generateShader_.isValid() ? "yes" : "stub",
             grassShader_.isValid() ? "yes" : "stub");
    return true;
}

void GrassSystem::bindHeightmap(ghi::TextureHandle heightmap, float terrainSize) {
    heightmap_ = heightmap;
    terrainSize_ = terrainSize;
}

void GrassSystem::bindTrailField(TrailField* trailField) {
    trailField_ = trailField;
}

void GrassSystem::resetCounters() {
    if (!initialized_ || !counterBuffer_.isValid()) return;

    // Reset counter to 0
    uint32_t zero = 0;
    ghi::updateBuffer(counterBuffer_, 0, sizeof(uint32_t), &zero);

    // Reset indirect draw command
    DrawIndirectCommand cmd = {
        GRASS_BLADE_VERTICES,  // vertexCount (per blade)
        0,                      // instanceCount (will be filled by compute)
        0,                      // firstVertex
        0                       // firstInstance
    };
    ghi::updateBuffer(indirectBuffer_, 0, sizeof(cmd), &cmd);
}

void GrassSystem::generateInstances(const glm::vec3& cameraPos, float deltaTime) {
    if (!initialized_ || !enabled_) return;

    totalTime_ += deltaTime;

    if (!generateShader_.isValid()) {
        // Shader not loaded - skip generation
        return;
    }

    // Bind compute shader
    ghi::bindComputeShader(generateShader_);

    // Bind resources
    // Set 0: Heightmap
    if (heightmap_.isValid()) {
        ghi::bindTexture(heightmap_, 0, 0);
    }

    // Set 0: Trail textures
    if (trailField_) {
        ghi::bindTexture(trailField_->getIntensityTexture(), 0, 1);
        ghi::bindTexture(trailField_->getDirectionTexture(), 0, 2);
    }

    // Set 0: Output buffers
    ghi::bindStorageBuffer(instanceBuffer_, 0, 3);
    ghi::bindStorageBuffer(counterBuffer_, 0, 4);

    // Calculate dispatch size based on grass radius
    uint32_t gridSize = static_cast<uint32_t>(params_.grassRadius * 2.0f / params_.cellSize);
    uint32_t groupsX = (gridSize + 7) / 8;
    uint32_t groupsY = (gridSize + 7) / 8;

    // Dispatch compute shader
    ghi::dispatch(groupsX, groupsY, 1);

    // Memory barrier: compute writes → indirect read + vertex read
    ghi::memoryBarrier();
}

void GrassSystem::draw() {
    if (!initialized_ || !enabled_) return;

    if (!grassShader_.isValid()) {
        // Shader not loaded - skip draw
        return;
    }

    // Bind instance buffer for vertex shader
    ghi::bindStorageBuffer(instanceBuffer_, 0, 0);

    // Use indirect draw
    ghi::drawIndirect(indirectBuffer_, 1, sizeof(DrawIndirectCommand));
}

void GrassSystem::setWind(const glm::vec3& direction, float speed, float strength) {
    windDirection_ = glm::normalize(direction);
    windSpeed_ = speed;
    windStrength_ = strength;
}

} // namespace ral
} // namespace rendering
} // namespace jupiter
