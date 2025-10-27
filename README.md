# Project Jupiter

Game engine framework with modular subsystems for game development. Features advanced rendering techniques inspired by modern game engines.

## Structure

### Core Libraries

- **platform** - Cross-platform abstraction layer (timing, file system, threading)
- **logging** - High-performance, lock-free logging system with tagged subsystems
- **math** - Vector math library with Vector2, Vector3, Vector4, Matrix4x4
- **memory** - Custom memory management (linear/stack allocators, memory tracking)
- **utils** - General utilities (string manipulation, file operations, timers)
- **event_system** - Decoupled event system for inter-subsystem communication

### Subsystem Libraries

- **core** - Core engine functionality and utilities
- **animation** - Animation system for characters and objects
- **rendering** - Advanced Vulkan graphics rendering subsystem
- **assets** - Configurable asset loading and management system
- **physics** - Physics simulation engine
- **audio** - Audio playback and sound management
- **input** - Input handling (keyboard, mouse, gamepad)
- **networking** - Client-server networking with TCP/UDP support
- **ui** - User interface system with buttons, labels, panels

### Vendored Libraries

Advanced reference implementations for future integration:

- **vulkan/** - SaschaWillems Vulkan examples (base utilities, swapchain, buffers)
- **hellovulkan/** - Advanced Vulkan rendering engine with:
  - Clustered Forward Shading
  - Physically-Based Rendering (PBR) with IBL
  - Hardware-Accelerated Path Tracing
  - Compute-Based Frustum Culling
  - GPU-Driven Rendering with Bindless techniques
  - Advanced Shadow Mapping (Cascade Shadow Maps)

### Projects

The `projects/` directory contains example game projects that use the subsystems:

- **hello_world** - Basic example demonstrating all subsystems

## Building

This project uses modern CMake (3.15+) with support for both static and shared library builds.

### Build Options

- `BUILD_SHARED_LIBS` - Build shared libraries (DLLs) instead of static libraries (default: OFF)
- `BUILD_PROJECTS` - Build example projects (default: ON)

### Quick Start

```bash
# Configure with default settings (static libraries)
cmake -B build -S .

# Build the project
cmake --build build

# Run the hello_world example
./build/bin/hello_world
```

### Building with Shared Libraries (DLLs)

```bash
# Configure with shared libraries
cmake -B build -S . -DBUILD_SHARED_LIBS=ON

# Build the project
cmake --build build

# Run the hello_world example
./build/bin/hello_world
```

### Build without Projects

```bash
# Configure without building example projects
cmake -B build -S . -DBUILD_PROJECTS=OFF

# Build only the libraries
cmake --build build
```

## Requirements

- CMake 3.15 or higher
- C++20 compatible compiler (GCC, Clang, MSVC)
- Vulkan SDK 1.3+ (for rendering subsystem)
- Git (for vendored submodules)

### Platform-Specific Requirements

#### macOS
- Vulkan SDK or MoltenVK framework
- Xcode command line tools

#### Linux
- Vulkan development packages
- XCB development libraries

#### Windows
- Vulkan SDK
- Windows SDK

## Advanced Features

### Rendering Pipeline Roadmap

The engine includes vendored implementations of advanced rendering techniques that can be integrated into the abstraction layer:

1. **Phase 1**: Clustered Forward Shading for efficient light culling
2. **Phase 2**: Physically-Based Rendering (PBR) with Image-Based Lighting
3. **Phase 3**: Hardware-Accelerated Path Tracing
4. **Phase 4**: GPU-Driven Rendering with bindless techniques

See `rendering/HELLOVULKAN_INTEGRATION.md` and `vendored/hellovulkan/README_PROJECT_JUPITER_INTEGRATION.md` for detailed integration guides.

### Asset System Configuration

The asset system supports runtime-configurable search paths:

```cpp
jupiter::assets::AssetConfig config;
config.searchPaths = {
    "assets/",           // Primary asset directory
    "../shared_assets/", // Shared assets
    "mods/",            // Mod support
};
config.enableCaching = true;
config.maxCacheSize = 100 * 1024 * 1024; // 100MB cache

jupiter::assets::initialize(config);
```

### Logging System

High-performance, lock-free logging with tagged subsystems:

```cpp
LOG_DEBUG("Physics", "Object velocity: %.2f", velocity);
LOG_INFO("Rendering", "Loaded %d textures", textureCount);
LOG_ERROR("Audio", "Failed to load sound: %s", filename);
```
