# Project Jupiter

Game engine framework with modular subsystems for game development.

## Structure

### Subsystem Libraries

The project is organized into several feature subsystems:

- **core** - Core engine functionality and utilities
- **animation** - Animation system for characters and objects
- **rendering** - Graphics rendering subsystem
- **assets** - Asset loading and management
- **physics** - Physics simulation engine
- **audio** - Audio playback and sound management
- **input** - Input handling (keyboard, mouse, gamepad)

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
- C++17 compatible compiler (GCC, Clang, MSVC)
