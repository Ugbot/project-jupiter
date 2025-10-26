#include <iostream>
#include "core/core.h"
#include "animation/animation.h"
#include "rendering/rendering.h"
#include "assets/assets.h"
#include "physics/physics.h"
#include "audio/audio.h"
#include "input/input.h"

int main() {
    std::cout << "=== Project Jupiter - Hello World ===" << std::endl;
    std::cout << std::endl;

    // Initialize all subsystems
    std::cout << "Initializing subsystems..." << std::endl;
    jupiter::core::initialize();
    jupiter::animation::initialize();
    jupiter::rendering::initialize();
    jupiter::assets::initialize();
    jupiter::physics::initialize();
    jupiter::audio::initialize();
    jupiter::input::initialize();
    std::cout << std::endl;

    // Display version
    std::cout << "Core version: " << jupiter::core::getVersion() << std::endl;
    std::cout << std::endl;

    // Simulate a basic game loop
    std::cout << "Running basic simulation..." << std::endl;
    jupiter::assets::loadAsset("example_asset.png");
    jupiter::rendering::clear();
    jupiter::rendering::renderFrame();
    jupiter::animation::update(0.016f);
    jupiter::physics::simulate(0.016f);
    jupiter::input::update();
    jupiter::audio::playSound("example_sound.wav");
    std::cout << std::endl;

    // Shutdown all subsystems
    std::cout << "Shutting down subsystems..." << std::endl;
    jupiter::input::shutdown();
    jupiter::audio::shutdown();
    jupiter::physics::shutdown();
    jupiter::assets::shutdown();
    jupiter::rendering::shutdown();
    jupiter::animation::shutdown();
    jupiter::core::shutdown();
    std::cout << std::endl;

    std::cout << "Hello World project completed successfully!" << std::endl;
    return 0;
}
