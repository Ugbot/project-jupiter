#pragma once

/**
 * @file grass_system.h
 * @brief GHI-based grass rendering system with GPU-driven instancing
 * 
 * Uses compute shaders to generate grass instances around the camera,
 * with trail-based flattening and bending effects.
 */

#include "rendering/ghi/ghi.h"
#include "rendering/ral/trail_field.h"
#include <glm/glm.hpp>

namespace jupiter {
namespace rendering {
namespace ral {

constexpr uint32_t MAX_GRASS_INSTANCES = 1024 * 1024;  // 1M instances max
constexpr uint32_t GRASS_BLADE_VERTICES = 12;          // 6 segments * 2 verts

/**
 * @brief Grass rendering parameters
 */
struct GrassParams {
    float grassRadius = 128.0f;      // Radius around camera
    float cellSize = 0.5f;           // Spacing between candidates
    float minHeight = 0.3f;          // Minimum blade height
    float maxHeight = 1.2f;          // Maximum blade height
    float densityMul = 1.0f;         // Density multiplier
    float minNormalY = 0.3f;         // Slope cutoff (steeper = no grass)
    float trailDensityKill = 0.7f;   // How much trail affects density
    float minHeightMul = 0.2f;       // Minimum height when flattened
};

/**
 * @brief GPU-generated grass rendering system
 * 
 * Uses compute shaders to:
 * 1. Generate grass instance positions around camera
 * 2. Apply heightmap displacement
 * 3. Apply trail field bending/flattening
 * 4. Output to instance buffer for indirect draw
 */
class GrassSystem {
public:
    GrassSystem() = default;
    ~GrassSystem();

    // Non-copyable
    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;

    /**
     * @brief Initialize grass system
     * 
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Shutdown and release resources
     */
    void shutdown();

    /**
     * @brief Bind terrain heightmap for grass placement
     * 
     * @param heightmap Heightmap texture
     * @param terrainSize Size of terrain in world units
     */
    void bindHeightmap(ghi::TextureHandle heightmap, float terrainSize);

    /**
     * @brief Bind trail field for grass interaction
     * 
     * @param trailField Trail field for bending/flattening
     */
    void bindTrailField(TrailField* trailField);

    /**
     * @brief Reset instance counter (call at start of frame)
     */
    void resetCounters();

    /**
     * @brief Generate grass instances (dispatch compute shader)
     * 
     * @param cameraPos Camera world position
     * @param deltaTime Delta time in seconds
     */
    void generateInstances(const glm::vec3& cameraPos, float deltaTime);

    /**
     * @brief Draw grass (indirect draw)
     * 
     * Must be called during render pass.
     */
    void draw();

    // Parameter access
    void setParams(const GrassParams& params) { params_ = params; }
    GrassParams& getParams() { return params_; }
    const GrassParams& getParams() const { return params_; }

    // Enable/disable
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // Wind settings
    void setWind(const glm::vec3& direction, float speed, float strength);

private:
    bool initialized_ = false;
    bool enabled_ = true;

    // Parameters
    GrassParams params_;
    float totalTime_ = 0.0f;

    // Wind
    glm::vec3 windDirection_ = glm::vec3(1.0f, 0.0f, 0.3f);
    float windSpeed_ = 0.5f;
    float windStrength_ = 0.3f;

    // GPU resources
    ghi::BufferHandle instanceBuffer_;    // Grass instance data
    ghi::BufferHandle indirectBuffer_;    // Indirect draw command
    ghi::BufferHandle counterBuffer_;     // Atomic instance counter

    // Compute shader
    ghi::ShaderHandle generateShader_;

    // Graphics shader for rendering
    ghi::ShaderHandle grassShader_;

    // External bindings
    ghi::TextureHandle heightmap_;
    float terrainSize_ = 1024.0f;
    TrailField* trailField_ = nullptr;

    bool createBuffers();
    bool createShaders();
};

} // namespace ral
} // namespace rendering
} // namespace jupiter
