# Vendored Dependencies Build Configuration
#
# This file configures all vendored third-party dependencies to build as
# static libraries integrated with the main build process.

cmake_minimum_required(VERSION 3.20)

message(STATUS "Configuring vendored dependencies...")

# ============================================================================
# SDL3 (Simple DirectMedia Layer)
# ============================================================================
# Purpose: Platform abstraction layer (windowing, audio, input, threading)
# License: zlib License
# URL: https://github.com/libsdl-org/SDL

message(STATUS "  -> Configuring SDL3...")

# Configure SDL3 to build as a static library only
set(SDL_SHARED OFF CACHE BOOL "Build SDL3 as shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL3 as static library" FORCE)

# Disable SDL3 tests and examples to speed up build
set(SDL_TEST OFF CACHE BOOL "Build SDL3 test programs" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL3 example programs" FORCE)

# Disable SDL3 components we don't need yet (can enable later)
# Keep audio, video, events, joystick, haptic for game engine use
set(SDL_RENDER ON CACHE BOOL "Enable SDL3 render API" FORCE)
set(SDL_AUDIO ON CACHE BOOL "Enable SDL3 audio subsystem" FORCE)
set(SDL_VIDEO ON CACHE BOOL "Enable SDL3 video subsystem" FORCE)
set(SDL_EVENTS ON CACHE BOOL "Enable SDL3 event subsystem" FORCE)
set(SDL_JOYSTICK ON CACHE BOOL "Enable SDL3 joystick subsystem" FORCE)
set(SDL_HAPTIC ON CACHE BOOL "Enable SDL3 haptic (force feedback) subsystem" FORCE)
set(SDL_SENSOR ON CACHE BOOL "Enable SDL3 sensor subsystem" FORCE)

# Add SDL3 to the build
add_subdirectory(sdl3)

# Set SDL3 include directories for other targets to use
set(SDL3_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdl3/include" PARENT_SCOPE)

message(STATUS "  -> SDL3 configured successfully")

# ============================================================================
# ImGui (Immediate Mode GUI)
# ============================================================================
# Purpose: Debug UI, editor interface, profiling overlays
# License: MIT License
# URL: https://github.com/ocornut/imgui

message(STATUS "  -> Configuring ImGui...")
add_subdirectory(imgui)
message(STATUS "  -> ImGui configured successfully")

# ============================================================================
# ImGuizmo (Gizmo manipulation for ImGui)
# ============================================================================
# Purpose: 3D transform gizmos for scene editing
# License: MIT License
# URL: https://github.com/CedricGuillemet/ImGuizmo

message(STATUS "  -> Configuring ImGuizmo...")
add_subdirectory(imguizmo)
message(STATUS "  -> ImGuizmo configured successfully")

# ============================================================================
# Tracy Profiler (optional)
# ============================================================================
# Purpose: High-performance CPU/GPU profiler with remote viewer
# License: 3-clause BSD
# URL: https://github.com/wolfpld/tracy

if(JUPITER_ENABLE_TRACY)
    message(STATUS "  -> Configuring Tracy profiler...")

    # Build Tracy as a static library and prefer on-demand capture to avoid
    # overhead when the viewer is disconnected.
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy instrumentation" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "Enable on-demand profiling" FORCE)
    set(TRACY_STATIC ON CACHE BOOL "Build Tracy as a static library" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "Restrict Tracy to localhost" FORCE)
    set(TRACY_NO_BROADCAST ON CACHE BOOL "Disable broadcast discovery" FORCE)

    add_subdirectory(tracy)

    # Surface common properties for downstream targets
    set(TRACY_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tracy/public" PARENT_SCOPE)
    set(TRACY_CLIENT_TARGET Tracy::TracyClient PARENT_SCOPE)

    message(STATUS "  -> Tracy profiler configured successfully")
else()
    message(STATUS "  -> Skipping Tracy profiler (JUPITER_ENABLE_TRACY=OFF)")
endif()

# ============================================================================
# Meshoptimizer
# ============================================================================
# Purpose: Mesh optimization library (LOD, vertex cache, simplification)
# License: MIT
# URL: https://github.com/zeux/meshoptimizer

message(STATUS "  -> Configuring meshoptimizer...")
add_subdirectory(meshoptimizer)
message(STATUS "  -> meshoptimizer configured successfully")

# ============================================================================
# Future vendored dependencies go here
# ============================================================================

message(STATUS "Vendored dependencies configured")
