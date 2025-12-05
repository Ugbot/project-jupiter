#pragma once

#include <array>
#include <vector>
#include <cstdint>

/**
 * @file lighting.h
 * @brief Forward rendering lighting system
 *
 * Supports multiple light types for forward rendering pipeline.
 * Following CLAUDE.md principles:
 * - Data-oriented design (SoA for light arrays)
 * - Lock-free updates
 * - No runtime allocations in hot paths
 */

namespace jupiter {
namespace rendering {

// Maximum number of lights supported per frame
constexpr uint32_t MAX_LIGHTS = 16;

/**
 * @brief Light type enumeration
 */
enum class LightType : uint32_t {
    DIRECTIONAL = 0,
    POINT = 1,
    SPOT = 2,
    AMBIENT = 3
};

/**
 * @brief Directional light (sun, moon)
 *
 * Infinite distance light with parallel rays.
 * Direction is normalized vector pointing TOWARDS the light source.
 */
struct DirectionalLight {
    float direction[3];      // Direction vector (normalized)
    float _padding0;
    float color[3];          // RGB color
    float intensity;         // Light intensity multiplier

    DirectionalLight()
        : direction{0.0f, -1.0f, 0.0f}
        , _padding0(0.0f)
        , color{1.0f, 1.0f, 1.0f}
        , intensity(1.0f) {}
};

/**
 * @brief Point light (bulb, torch)
 *
 * Omnidirectional light with distance attenuation.
 * Uses inverse-square falloff by default.
 */
struct PointLight {
    float position[3];       // World position
    float _padding0;
    float color[3];          // RGB color
    float intensity;         // Light intensity multiplier
    float constant;          // Constant attenuation factor
    float linear;            // Linear attenuation factor
    float quadratic;         // Quadratic attenuation factor
    float radius;            // Maximum light range

    PointLight()
        : position{0.0f, 0.0f, 0.0f}
        , _padding0(0.0f)
        , color{1.0f, 1.0f, 1.0f}
        , intensity(1.0f)
        , constant(1.0f)
        , linear(0.09f)
        , quadratic(0.032f)
        , radius(10.0f) {}
};

/**
 * @brief Spot light (flashlight, car headlight)
 *
 * Directional light with cone-shaped falloff.
 */
struct SpotLight {
    float position[3];       // World position
    float _padding0;
    float direction[3];      // Direction vector (normalized)
    float _padding1;
    float color[3];          // RGB color
    float intensity;         // Light intensity multiplier
    float innerConeAngle;    // Inner cone angle (radians)
    float outerConeAngle;    // Outer cone angle (radians)
    float constant;          // Constant attenuation factor
    float linear;            // Linear attenuation factor
    float quadratic;         // Quadratic attenuation factor
    float _padding2;

    SpotLight()
        : position{0.0f, 0.0f, 0.0f}
        , _padding0(0.0f)
        , direction{0.0f, -1.0f, 0.0f}
        , _padding1(0.0f)
        , color{1.0f, 1.0f, 1.0f}
        , intensity(1.0f)
        , innerConeAngle(0.436f)  // ~25 degrees
        , outerConeAngle(0.524f)  // ~30 degrees
        , constant(1.0f)
        , linear(0.09f)
        , quadratic(0.032f)
        , _padding2(0.0f) {}
};

/**
 * @brief Ambient light (global illumination approximation)
 */
struct AmbientLight {
    float color[3];          // RGB color
    float intensity;         // Light intensity multiplier

    AmbientLight()
        : color{0.1f, 0.1f, 0.1f}
        , intensity(1.0f) {}
};

/**
 * @brief Unified light data for shader UBO
 *
 * Uses discriminated union pattern for efficient GPU upload.
 * Type field determines which light data is valid.
 */
struct alignas(16) Light {
    uint32_t type;           // LightType enum value
    float _padding0[3];

    // Union of all light types (largest structure determines size)
    union {
        struct {
            float direction[3];
            float _pad0;
            float color[3];
            float intensity;
        } directional;

        struct {
            float position[3];
            float _pad0;
            float color[3];
            float intensity;
            float constant;
            float linear;
            float quadratic;
            float radius;
        } point;

        struct {
            float position[3];
            float _pad0;
            float direction[3];
            float _pad1;
            float color[3];
            float intensity;
            float innerConeAngle;
            float outerConeAngle;
            float constant;
            float linear;
            float quadratic;
            float _pad2;
        } spot;

        struct {
            float color[3];
            float intensity;
        } ambient;
    };

    Light() : type(static_cast<uint32_t>(LightType::AMBIENT)) {
        ambient.color[0] = 0.1f;
        ambient.color[1] = 0.1f;
        ambient.color[2] = 0.1f;
        ambient.intensity = 1.0f;
    }
};

/**
 * @brief Light manager for forward rendering
 *
 * Manages an array of lights and prepares data for GPU upload.
 * Thread-safe for reading, single-writer pattern for updates.
 */
class LightManager {
public:
    LightManager();
    ~LightManager() = default;

    // No copy, allow move
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    LightManager(LightManager&&) noexcept = default;
    LightManager& operator=(LightManager&&) noexcept = default;

    /**
     * @brief Add a directional light
     * @return Light index, or -1 if full
     */
    int32_t addDirectionalLight(const DirectionalLight& light);

    /**
     * @brief Add a point light
     * @return Light index, or -1 if full
     */
    int32_t addPointLight(const PointLight& light);

    /**
     * @brief Add a spot light
     * @return Light index, or -1 if full
     */
    int32_t addSpotLight(const SpotLight& light);

    /**
     * @brief Set ambient light
     */
    void setAmbientLight(const AmbientLight& light);

    /**
     * @brief Update existing light
     * @param index Light index from add*Light()
     * @param light New light data (type must match)
     * @return true if successful
     */
    bool updateLight(int32_t index, const Light& light);

    /**
     * @brief Remove light by index
     * @param index Light index from add*Light()
     */
    void removeLight(int32_t index);

    /**
     * @brief Clear all lights
     */
    void clear();

    /**
     * @brief Get light count
     */
    uint32_t getLightCount() const { return lightCount_; }

    /**
     * @brief Get lights array for GPU upload
     */
    const Light* getLights() const { return lights_.data(); }

    /**
     * @brief Get lights array size in bytes
     */
    size_t getLightsDataSize() const { return sizeof(Light) * MAX_LIGHTS; }

    /**
     * @brief Check if lights have been modified since last upload
     */
    bool isDirty() const { return dirty_; }

    /**
     * @brief Mark lights as uploaded (clear dirty flag)
     */
    void markClean() { dirty_ = false; }

private:
    std::array<Light, MAX_LIGHTS> lights_;
    uint32_t lightCount_;
    bool dirty_;

    int32_t findFreeSlot();
};

} // namespace rendering
} // namespace jupiter
