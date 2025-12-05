#pragma once

/**
 * @file simulation_runner.h
 * @brief Headless simulation runner for dedicated servers and testing
 *
 * SimulationRunner provides a game loop that runs ECS simulation
 * without any rendering. Perfect for:
 * - Dedicated game servers
 * - Headless testing
 * - Physics simulations
 * - AI training environments
 *
 * This module has NO dependency on the rendering module.
 * Rendering is completely optional and can be added separately.
 */

#include "world.h"
#include <functional>
#include <atomic>
#include <chrono>
#include <thread>
#include <memory>

namespace jupiter::ecs {

/**
 * @brief Headless simulation runner for the ECS World
 *
 * Runs the ECS simulation loop without any GPU/rendering dependencies.
 * The simulation can be connected to a renderer on another machine
 * via networking, or run completely standalone for servers.
 *
 * Thread Safety:
 * - run() blocks on the calling thread
 * - stop() can be called from any thread
 * - tick() is single-threaded (call from game loop)
 *
 * Usage:
 * @code
 *   SimulationRunner runner(60);  // 60 ticks per second
 *   runner.initialize();
 *
 *   // Register simulation callbacks
 *   runner.setUpdateCallback([](World& world, float dt) {
 *       // Your game logic here
 *       executePhysicsKernel(world);
 *       executeAIKernel(world);
 *   });
 *
 *   runner.run();  // Blocks until stop() called
 * @endcode
 *
 * Manual tick control:
 * @code
 *   SimulationRunner runner(60);
 *   runner.initialize();
 *   runner.setUpdateCallback(myCallback);
 *
 *   while (gameRunning) {
 *       runner.tick();  // Single tick
 *       // ... handle networking, etc.
 *   }
 * @endcode
 */
class SimulationRunner {
public:
    using UpdateCallback = std::function<void(World&, float)>;

    /**
     * @brief Construct a simulation runner
     * @param tickRate Simulation ticks per second (default 60)
     * @param maxEntities Maximum entity capacity
     */
    explicit SimulationRunner(uint32_t tickRate = 60, uint32_t maxEntities = 8192);
    ~SimulationRunner() = default;

    // Non-copyable
    SimulationRunner(const SimulationRunner&) = delete;
    SimulationRunner& operator=(const SimulationRunner&) = delete;

    /**
     * @brief Initialize the ECS world
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Initialize with an existing world (transfers ownership)
     * @param world World to use
     * @return true if successful
     */
    bool initialize(std::unique_ptr<World> world);

    /**
     * @brief Set simulation update callback
     * @param callback Called every tick with world and delta time
     */
    void setUpdateCallback(UpdateCallback callback) {
        updateCallback_ = std::move(callback);
    }

    /**
     * @brief Run the simulation loop (blocking)
     *
     * Runs until stop() is called from another thread.
     * Maintains fixed tick rate with sleep.
     */
    void run();

    /**
     * @brief Run a single simulation tick
     * @return true if running, false if stopped
     *
     * Use this for manual tick control instead of run().
     */
    bool tick();

    /**
     * @brief Stop the simulation loop
     *
     * Thread-safe. Can be called from any thread.
     */
    void stop() { running_.store(false, std::memory_order_release); }

    /**
     * @brief Check if running
     */
    [[nodiscard]] bool isRunning() const {
        return running_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get the ECS world for direct access
     */
    [[nodiscard]] World& getWorld() { return *world_; }
    [[nodiscard]] const World& getWorld() const { return *world_; }

    /**
     * @brief Get pointer to world (for passing to renderer bridge)
     */
    [[nodiscard]] World* getWorldPtr() { return world_.get(); }

    /**
     * @brief Get current tick count
     */
    [[nodiscard]] uint64_t tickCount() const { return tickCount_; }

    /**
     * @brief Get tick rate (ticks per second)
     */
    [[nodiscard]] uint32_t tickRate() const { return tickRate_; }

    /**
     * @brief Get simulation time in seconds
     */
    [[nodiscard]] double simulationTime() const {
        return static_cast<double>(tickCount_) / static_cast<double>(tickRate_);
    }

    /**
     * @brief Get time per tick in seconds
     */
    [[nodiscard]] float deltaTime() const {
        return 1.0f / static_cast<float>(tickRate_);
    }

private:
    uint32_t tickRate_;
    uint32_t maxEntities_;
    std::unique_ptr<World> world_;
    UpdateCallback updateCallback_;
    std::atomic<bool> running_{false};
    uint64_t tickCount_ = 0;
};

} // namespace jupiter::ecs
