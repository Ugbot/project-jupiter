#include "rendering/lighting.h"
#include "logging/logging.h"
#include <cstring>

namespace jupiter {
namespace rendering {

LightManager::LightManager()
    : lightCount_(0)
    , dirty_(false) {
    // Initialize all lights to inactive
    for (auto& light : lights_) {
        light.type = static_cast<uint32_t>(LightType::AMBIENT);
        light.ambient.color[0] = 0.0f;
        light.ambient.color[1] = 0.0f;
        light.ambient.color[2] = 0.0f;
        light.ambient.intensity = 0.0f;
    }
}

int32_t LightManager::addDirectionalLight(const DirectionalLight& light) {
    int32_t index = findFreeSlot();
    if (index < 0) {
        LOG_WARN("LightManager", "Cannot add directional light - maximum lights reached");
        return -1;
    }

    lights_[index].type = static_cast<uint32_t>(LightType::DIRECTIONAL);
    std::memcpy(lights_[index].directional.direction, light.direction, sizeof(float) * 3);
    std::memcpy(lights_[index].directional.color, light.color, sizeof(float) * 3);
    lights_[index].directional.intensity = light.intensity;

    lightCount_++;
    dirty_ = true;

    LOG_INFO("LightManager", "Added directional light at index %d", index);
    return index;
}

int32_t LightManager::addPointLight(const PointLight& light) {
    int32_t index = findFreeSlot();
    if (index < 0) {
        LOG_WARN("LightManager", "Cannot add point light - maximum lights reached");
        return -1;
    }

    lights_[index].type = static_cast<uint32_t>(LightType::POINT);
    std::memcpy(lights_[index].point.position, light.position, sizeof(float) * 3);
    std::memcpy(lights_[index].point.color, light.color, sizeof(float) * 3);
    lights_[index].point.intensity = light.intensity;
    lights_[index].point.constant = light.constant;
    lights_[index].point.linear = light.linear;
    lights_[index].point.quadratic = light.quadratic;
    lights_[index].point.radius = light.radius;

    lightCount_++;
    dirty_ = true;

    LOG_INFO("LightManager", "Added point light at index %d", index);
    return index;
}

int32_t LightManager::addSpotLight(const SpotLight& light) {
    int32_t index = findFreeSlot();
    if (index < 0) {
        LOG_WARN("LightManager", "Cannot add spot light - maximum lights reached");
        return -1;
    }

    lights_[index].type = static_cast<uint32_t>(LightType::SPOT);
    std::memcpy(lights_[index].spot.position, light.position, sizeof(float) * 3);
    std::memcpy(lights_[index].spot.direction, light.direction, sizeof(float) * 3);
    std::memcpy(lights_[index].spot.color, light.color, sizeof(float) * 3);
    lights_[index].spot.intensity = light.intensity;
    lights_[index].spot.innerConeAngle = light.innerConeAngle;
    lights_[index].spot.outerConeAngle = light.outerConeAngle;
    lights_[index].spot.constant = light.constant;
    lights_[index].spot.linear = light.linear;
    lights_[index].spot.quadratic = light.quadratic;

    lightCount_++;
    dirty_ = true;

    LOG_INFO("LightManager", "Added spot light at index %d", index);
    return index;
}

void LightManager::setAmbientLight(const AmbientLight& light) {
    // Ambient light stored in first slot if available
    if (lights_.empty()) {
        return;
    }

    // Find or create ambient light slot (prefer index 0)
    int32_t ambientIndex = -1;
    for (uint32_t i = 0; i < MAX_LIGHTS; ++i) {
        if (lights_[i].type == static_cast<uint32_t>(LightType::AMBIENT) &&
            lights_[i].ambient.intensity > 0.0f) {
            ambientIndex = i;
            break;
        }
    }

    if (ambientIndex < 0) {
        ambientIndex = findFreeSlot();
        if (ambientIndex < 0) {
            LOG_WARN("LightManager", "Cannot set ambient light - maximum lights reached");
            return;
        }
        lightCount_++;
    }

    lights_[ambientIndex].type = static_cast<uint32_t>(LightType::AMBIENT);
    std::memcpy(lights_[ambientIndex].ambient.color, light.color, sizeof(float) * 3);
    lights_[ambientIndex].ambient.intensity = light.intensity;

    dirty_ = true;
    LOG_INFO("LightManager", "Set ambient light");
}

bool LightManager::updateLight(int32_t index, const Light& light) {
    if (index < 0 || index >= static_cast<int32_t>(MAX_LIGHTS)) {
        LOG_WARN("LightManager", "Invalid light index: %d", index);
        return false;
    }

    // Verify type matches
    if (lights_[index].type != light.type) {
        LOG_WARN("LightManager", "Light type mismatch at index %d", index);
        return false;
    }

    lights_[index] = light;
    dirty_ = true;
    return true;
}

void LightManager::removeLight(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(MAX_LIGHTS)) {
        return;
    }

    // Check if light is active
    if (lights_[index].type == static_cast<uint32_t>(LightType::AMBIENT) &&
        lights_[index].ambient.intensity == 0.0f) {
        return; // Already inactive
    }

    // Deactivate light
    lights_[index].type = static_cast<uint32_t>(LightType::AMBIENT);
    lights_[index].ambient.color[0] = 0.0f;
    lights_[index].ambient.color[1] = 0.0f;
    lights_[index].ambient.color[2] = 0.0f;
    lights_[index].ambient.intensity = 0.0f;

    if (lightCount_ > 0) {
        lightCount_--;
    }
    dirty_ = true;

    LOG_INFO("LightManager", "Removed light at index %d", index);
}

void LightManager::clear() {
    for (auto& light : lights_) {
        light.type = static_cast<uint32_t>(LightType::AMBIENT);
        light.ambient.color[0] = 0.0f;
        light.ambient.color[1] = 0.0f;
        light.ambient.color[2] = 0.0f;
        light.ambient.intensity = 0.0f;
    }
    lightCount_ = 0;
    dirty_ = true;

    LOG_INFO("LightManager", "Cleared all lights");
}

int32_t LightManager::findFreeSlot() {
    for (uint32_t i = 0; i < MAX_LIGHTS; ++i) {
        // A slot is free if it's an inactive ambient light
        if (lights_[i].type == static_cast<uint32_t>(LightType::AMBIENT) &&
            lights_[i].ambient.intensity == 0.0f) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

} // namespace rendering
} // namespace jupiter
