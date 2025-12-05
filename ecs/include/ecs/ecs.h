#pragma once

/**
 * @file ecs.h
 * @brief Main header for Jupiter ECS module
 * 
 * Includes all ECS components for convenient single-include usage.
 * 
 * Architecture:
 * - Arrow-inspired columnar storage with vectorized kernels
 * - Double-buffered (tick-tock) for multi-reader concurrency
 * - Clone-on-write dirty tracking
 * - SIMD-optimized batch operations
 * 
 * Threading Model:
 * - N reader threads can read simultaneously via ReadSnapshot
 * - 1 writer thread modifies via World::writeBuffer()
 * - World::swap() atomically switches buffers
 * 
 * Example Usage:
 * @code
 * #include <ecs/ecs.h>
 * 
 * using namespace jupiter::ecs;
 * 
 * // Create world with capacity for 10000 entities
 * World world(10000);
 * 
 * // Register built-in kernels
 * kernels::registerBuiltinKernels();
 * 
 * // Create an entity
 * EntityCreateInfo info;
 * info.position = glm::vec3(0, 1, 0);
 * info.flags = EntityFlags::Active | EntityFlags::Physics;
 * info.mass = 1.0f;
 * uint32_t idx = world.create(info);
 * 
 * // Game loop
 * while (running) {
 *     // Reader threads can snapshot
 *     ReadSnapshot snap = world.acquireReadSnapshot();
 *     // ... render using snap.transforms(), etc.
 * 
 *     // Writer thread updates
 *     KernelContext ctx;
 *     ctx.deltaTime = 0.016f;
 *     KernelRegistry::instance().execute("gravity", world, ctx);
 *     KernelRegistry::instance().execute("physics_integrate", world, ctx);
 *     KernelRegistry::instance().execute("transform", world, ctx);
 * 
 *     world.processQueued();
 *     world.swap();
 * }
 * @endcode
 */

// Core types
#include "types.h"
#include "span.h"

// Kernel system
#include "exec_batch.h"
#include "kernel.h"
#include "kernel_registry.h"

// Storage
#include "column_storage.h"
#include "entity_buffer.h"

// World and snapshots
#include "world.h"
#include "read_snapshot.h"

// SIMD operations
#include "simd_ops.h"

// Built-in kernels
#include "builtin_kernels.h"

// Rendering integration
#include "render_batch.h"

namespace jupiter::ecs {

/**
 * @brief Initialize the ECS module
 * 
 * Registers all built-in kernels with the registry.
 * Call once at application startup.
 */
inline void initialize() {
    kernels::registerBuiltinKernels();
}

/**
 * @brief Get ECS module version string
 */
inline const char* version() {
    return "1.0.0";
}

} // namespace jupiter::ecs

