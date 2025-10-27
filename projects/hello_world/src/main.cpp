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
    jupiter::physics::initialize();
    jupiter::audio::initialize();
    jupiter::input::initialize();

    // Initialize assets with configurable paths
    jupiter::assets::AssetConfig assetConfig;
    assetConfig.searchPaths = {
        "assets/",              // Build directory assets (copied by CMake)
        "../assets/",           // Source directory assets
        "./",                   // Current directory
        "resources/",           // Alternative resource directory
    };
    assetConfig.enableCaching = true;
    assetConfig.maxCacheSize = 50 * 1024 * 1024; // 50MB cache
    assetConfig.fallbackPath = "../assets/"; // Fallback for missing assets

    jupiter::assets::initialize(assetConfig);
    std::cout << std::endl;

    // Display version
    std::cout << "Core version: " << jupiter::core::getVersion() << std::endl;
    std::cout << std::endl;

    // Simulate a basic game loop
    std::cout << "Running basic simulation..." << std::endl;

    // Demonstrate new asset system with configurable paths
    auto& assetManager = jupiter::assets::getAssetManager();

    // Load assets by name (will search through configured paths)
    auto textureAsset = assetManager.loadAsset("textures/example_texture.png", jupiter::assets::AssetType::TEXTURE);
    auto audioAsset = assetManager.loadAsset("audio/example_sound.wav", jupiter::assets::AssetType::AUDIO);

    if (textureAsset) {
        std::cout << "Successfully loaded texture: " << textureAsset->getName()
                  << " (" << textureAsset->getSize() << " bytes)" << std::endl;
    } else {
        std::cout << "Texture not found in configured paths" << std::endl;
    }

    if (audioAsset) {
        std::cout << "Successfully loaded audio: " << audioAsset->getName()
                  << " (" << audioAsset->getSize() << " bytes)" << std::endl;
    } else {
        std::cout << "Audio not found in configured paths" << std::endl;
    }

    // Show asset manager statistics
    auto stats = assetManager.getCacheStats();
    std::cout << "Asset cache: " << stats.cachedAssets << " assets, "
              << stats.cacheSizeBytes << " bytes used" << std::endl;

    // Note: Rendering subsystem now uses Renderer class - window system integration needed
    std::cout << "Rendering frame..." << std::endl;
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
    // jupiter::rendering::shutdown(); // Now handled by Renderer class
    jupiter::animation::shutdown();
    jupiter::core::shutdown();
    std::cout << std::endl;

    std::cout << "Hello World project completed successfully!" << std::endl;
    return 0;
}
