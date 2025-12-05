/**
 * @file simulation_runner.cpp
 * @brief Implementation of headless simulation runner
 */

#include "ecs/simulation_runner.h"

namespace jupiter::ecs {

SimulationRunner::SimulationRunner(uint32_t tickRate, uint32_t maxEntities)
    : tickRate_(tickRate)
    , maxEntities_(maxEntities) {
}

bool SimulationRunner::initialize() {
    world_ = std::make_unique<World>(maxEntities_);
    return world_ != nullptr;
}

bool SimulationRunner::initialize(std::unique_ptr<World> world) {
    if (!world) {
        return false;
    }
    world_ = std::move(world);
    return true;
}

void SimulationRunner::run() {
    if (!world_) {
        return;
    }

    running_.store(true, std::memory_order_release);

    const auto tickDuration = std::chrono::microseconds(1000000 / tickRate_);
    auto lastTime = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_acquire)) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Process deferred entity operations
        world_->processQueued();

        // Run user update callback
        if (updateCallback_) {
            updateCallback_(*world_, elapsed);
        }

        // Swap buffers (make changes visible to readers)
        world_->swap();

        ++tickCount_;

        // Maintain fixed tick rate
        auto tickEnd = std::chrono::steady_clock::now();
        auto tickTime = tickEnd - now;
        if (tickTime < tickDuration) {
            std::this_thread::sleep_for(tickDuration - tickTime);
        }
    }
}

bool SimulationRunner::tick() {
    if (!world_ || !running_.load(std::memory_order_acquire)) {
        return false;
    }

    // Fixed delta time based on tick rate
    float dt = deltaTime();

    // Process deferred entity operations
    world_->processQueued();

    // Run user update callback
    if (updateCallback_) {
        updateCallback_(*world_, dt);
    }

    // Swap buffers
    world_->swap();

    ++tickCount_;
    return true;
}

} // namespace jupiter::ecs
