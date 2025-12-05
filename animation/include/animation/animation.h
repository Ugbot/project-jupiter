#pragma once

#include "animation/animation_export.h"

namespace jupiter {
namespace animation {

/**
 * @brief Initialize the animation subsystem
 * @return true if initialization was successful, false otherwise
 */
ANIMATION_API bool initialize();

/**
 * @brief Shutdown the animation subsystem
 */
ANIMATION_API void shutdown();

/**
 * @brief Update all animations
 * @param deltaTime Time elapsed since last update in seconds
 */
ANIMATION_API void update(float deltaTime);

} // namespace animation
} // namespace jupiter

// Include animation system headers
#include "animation/skeleton.h"
#include "animation/animation_clip.h"
