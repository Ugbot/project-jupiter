# Vendored Dependencies

This directory contains vendored third-party dependencies that are built as static libraries and bundled with the engine.

## Purpose

- **Static Linking**: All dependencies are built as static libraries to avoid runtime dependencies
- **Cross-platform**: Dependencies are compiled for the target platform
- **Controlled Updates**: Dependencies are updated manually and tested thoroughly
- **Build Integration**: Dependencies are built as part of the main build process

## Adding New Dependencies

1. Create a subdirectory for the dependency (e.g., `vendored/zlib/`)
2. Add source code and build files
3. Create a CMakeLists.txt that builds the dependency as a static library
4. Update the main CMakeLists.txt to include the vendored dependency

## Current Dependencies

### SDL3 (Simple DirectMedia Layer)
- **Version**: 3.x (main branch)
- **Purpose**: Platform abstraction (windowing, audio, input, threading)
- **License**: zlib License
- **URL**: https://github.com/libsdl-org/SDL
- **Status**: Active - Primary platform abstraction layer
- **Replaces**: GLFW (windowing only)

### GLFW (Graphics Library Framework)
- **Version**: 3.x
- **Purpose**: Windowing (legacy, being replaced by SDL3)
- **License**: zlib License
- **Status**: ⚠️ DEPRECATED - Will be removed after SDL3 migration complete
- **Migration**: See SDL_MIGRATION_GUIDE.md

### Vulkan SDK
- **Purpose**: Graphics API headers and validation layers
- **License**: Apache 2.0
- **Status**: Active - Used alongside SDL3 for rendering
- **Note**: Includes MoltenVK for macOS support

### SQLite
- **Version**: 3.48.0 (amalgamation)
- **Purpose**: Asset database for metadata and dependency tracking
- **License**: Public Domain
- **URL**: https://www.sqlite.org/
- **Status**: Active - Asset management system

### stb_image
- **Purpose**: Single-header image loading library
- **License**: Public Domain (MIT)
- **URL**: https://github.com/nothings/stb
- **Status**: Active - Texture loading

### tinygltf
- **Purpose**: GLTF 2.0 mesh/scene loader
- **License**: MIT
- **URL**: https://github.com/syoyo/tinygltf
- **Status**: Active - GLTF mesh loading

### Assimp
- **Purpose**: Comprehensive 3D asset importer
- **License**: BSD
- **URL**: https://github.com/assimp/assimp
- **Status**: Active - Supports OBJ, FBX, COLLADA, etc.

## Reference-Only Dependencies

These dependencies are included for reference and study purposes but are **NOT** compiled into the engine:

### librg (REFERENCE ONLY)
- **Version**: Latest from main branch
- **Purpose**: Reference for netcode architecture (entity replication, interest management)
- **License**: Apache 2.0
- **URL**: https://github.com/zpl-c/librg
- **Status**: 📚 REFERENCE ONLY - Not built or linked
- **Note**: Monolithic C approach - we're building a better C++ solution following CLAUDE.md principles

### Vulkan-glTF-PBR (REFERENCE ONLY)
- **Version**: Latest from master branch
- **Purpose**: Reference implementation for PBR rendering with glTF 2.0
- **License**: MIT
- **URL**: https://github.com/SaschaWillems/Vulkan-glTF-PBR
- **Status**: 📚 REFERENCE ONLY - Not built or linked
- **Note**: Production-quality PBR shader implementation by Sascha Willems
- **Features**:
  - Complete Cook-Torrance BRDF
  - Image-based lighting (IBL)
  - HDR environment maps
  - Proper tangent-space normal mapping
  - Reference for debugging lighting issues

## Build Requirements

All vendored dependencies should:
- Build as static libraries (`.a` or `.lib`)
- Not require external system libraries (except standard C/C++ libraries)
- Be compatible with C++20
- Support the target platforms (Windows, macOS, Linux)
