cmake_minimum_required(VERSION 3.15)

project(ProjectJupiter
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "Game Engine Framework"
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs) instead of static libraries" OFF)
option(BUILD_PROJECTS "Build example projects" ON)
option(JUPITER_ENABLE_TRACY "Enable Tracy profiler instrumentation" ON)

include(CTest)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Vendored third-party dependencies (must be built first)
add_subdirectory(vendored)

# Profiling utilities (Tracy wrapper)
add_subdirectory(profiling)

# Add core libraries
add_subdirectory(platform)
add_subdirectory(logging)
add_subdirectory(math)
add_subdirectory(memory)
add_subdirectory(utils)
add_subdirectory(event_system)
add_subdirectory(ecs)

# Add subsystems
add_subdirectory(rendering)
add_subdirectory(animation)
add_subdirectory(assets)
add_subdirectory(physics)
add_subdirectory(audio)
add_subdirectory(input)
add_subdirectory(core)
add_subdirectory(networking)
add_subdirectory(ui)
add_subdirectory(scripting)
add_subdirectory(voxel)

# Copy assets to build directory
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "Copying assets to build directory"
)

# Add projects if enabled
if(BUILD_PROJECTS)
    add_subdirectory(projects)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
