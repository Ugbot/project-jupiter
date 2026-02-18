/**
 * @file ghi.h
 * @brief Graphics Hardware Interface - Convenience header
 * 
 * Single include for all GHI/RAL functionality plus GLM math.
 * Applications should include this header to get:
 * - GHI (low-level graphics API)
 * - RAL (high-level rendering layer)
 * - GLM (math types - vec3, mat4, etc.)
 * - Primitives (test geometry generators)
 */

#pragma once

// Math types (vec3, mat4, etc.) - always available
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// GHI - Low-level graphics hardware interface
#include "rendering/ghi/ghi.h"
#include "rendering/ghi/ghi_types.h"

// RAL - High-level render abstraction layer
#include "rendering/ral/ral.h"
#include "rendering/ral/ral_types.h"

// Primitives - Test geometry generators
#include "rendering/primitives.h"

// Pipelines
#include "rendering/pipelines/pipeline_simple.h"

namespace jupiter {
namespace rendering {

/**
 * @brief Initialize the rendering system
 * 
 * @param backend Backend to use (Metal, Vulkan, OpenGL)
 * @return true if initialization succeeded
 */
inline bool initialize(ghi::Backend backend = ghi::Backend::Metal) {
    if (!ghi::initialize(backend)) {
        return false;
    }
    
    if (!ral::initialize()) {
        ghi::shutdown();
        return false;
    }
    
    return true;
}

/**
 * @brief Shutdown the rendering system
 */
inline void shutdown() {
    ral::shutdown();
    ghi::shutdown();
}

} // namespace rendering
} // namespace jupiter

