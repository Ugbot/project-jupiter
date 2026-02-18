#pragma once

/**
 * @file trail_field.h
 * @brief GHI-based trail field for grass flattening and bending
 * 
 * GPU trail field using GHI abstraction layer for cross-platform compute.
 * Manages ping-pong textures for trail intensity and direction.
 */

#include "rendering/ghi/ghi.h"
#include <glm/glm.hpp>
#include <vector>

namespace jupiter {
namespace rendering {
namespace ral {

constexpr uint32_t MAX_TRAIL_EVENTS = 32;

/**
 * @brief Trail event for CPU→GPU staging
 */
struct TrailEvent {
    glm::vec4 posRadius;   // xyz=world pos (x,z), w=radius in meters
    glm::vec4 dirBend;     // xy=direction, z=bendStrength, w=flattenStrength
};

/**
 * @brief Trail field configuration
 */
struct TrailFieldConfig {
    float worldSize = 256.0f;      // Size of trail region in meters
    uint32_t resolution = 512;      // Texture resolution
    float relaxSeconds = 12.0f;     // Time for trails to fade (5-20s)
};

/**
 * @brief GPU trail field for grass/vegetation interaction
 * 
 * Uses GHI compute shaders with ping-pong textures to track:
 * - Trail intensity (how flattened the grass is)
 * - Trail direction (which way grass bends)
 * 
 * Supports programmable relaxation (trails fade over 5-20 seconds).
 */
class TrailField {
public:
    TrailField() = default;
    ~TrailField();

    // Non-copyable
    TrailField(const TrailField&) = delete;
    TrailField& operator=(const TrailField&) = delete;

    /**
     * @brief Initialize trail field resources
     * 
     * @param config Trail field configuration
     * @return true if successful
     */
    bool initialize(const TrailFieldConfig& config = TrailFieldConfig{});

    /**
     * @brief Shutdown and release resources
     */
    void shutdown();

    /**
     * @brief Push a trail event (CPU staging)
     */
    void pushEvent(const TrailEvent& event);

    /**
     * @brief Clear all events for this frame
     */
    void clearEvents();

    /**
     * @brief Update trail field (dispatch compute shader)
     * 
     * Call during frame update, before grass rendering.
     * 
     * @param dtSeconds Delta time in seconds
     * @param newOrigin New world origin (bottom-left of trail region)
     */
    void update(float dtSeconds, const glm::vec2& newOrigin);

    /**
     * @brief Set relaxation time (5-20s)
     */
    void setRelaxSeconds(float seconds);

    // Getters
    ghi::TextureHandle getIntensityTexture() const { return currentIntensity_; }
    ghi::TextureHandle getDirectionTexture() const { return currentDir_; }
    float getWorldSize() const { return config_.worldSize; }
    glm::vec2 getOrigin() const { return currentOrigin_; }

private:
    TrailFieldConfig config_;
    bool initialized_ = false;

    // Ping-pong textures (intensity and direction)
    ghi::TextureHandle intensityA_;
    ghi::TextureHandle intensityB_;
    ghi::TextureHandle dirA_;
    ghi::TextureHandle dirB_;

    // Current/previous pointers for ping-pong
    ghi::TextureHandle currentIntensity_;
    ghi::TextureHandle currentDir_;
    ghi::TextureHandle prevIntensity_;
    ghi::TextureHandle prevDir_;

    // Events storage buffer
    ghi::BufferHandle eventsBuffer_;
    std::vector<TrailEvent> eventsStaging_;

    // Compute shader
    ghi::ShaderHandle updateShader_;

    // State
    glm::vec2 currentOrigin_ = glm::vec2(0.0f);
    glm::vec2 prevOrigin_ = glm::vec2(0.0f);
    uint32_t pingPong_ = 0;

    bool createTextures();
    bool createBuffers();
    bool createShader();
};

} // namespace ral
} // namespace rendering
} // namespace jupiter
