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

*(To be added as needed)*

## Build Requirements

All vendored dependencies should:
- Build as static libraries (`.a` or `.lib`)
- Not require external system libraries (except standard C/C++ libraries)
- Be compatible with C++20
- Support the target platforms (Windows, macOS, Linux)
