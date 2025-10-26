#pragma once

#ifdef _WIN32
    #ifdef PHYSICS_EXPORTS
        #define PHYSICS_API __declspec(dllexport)
    #elif defined(PHYSICS_IMPORTS)
        #define PHYSICS_API __declspec(dllimport)
    #else
        #define PHYSICS_API
    #endif
#else
    #define PHYSICS_API
#endif

namespace jupiter {
namespace physics {

/**
 * @brief Initialize the physics subsystem
 * @return true if initialization was successful, false otherwise
 */
PHYSICS_API bool initialize();

/**
 * @brief Shutdown the physics subsystem
 */
PHYSICS_API void shutdown();

/**
 * @brief Simulate physics for one step
 * @param deltaTime Time step for simulation in seconds
 */
PHYSICS_API void simulate(float deltaTime);

} // namespace physics
} // namespace jupiter
