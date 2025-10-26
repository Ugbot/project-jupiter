cmake_minimum_required(VERSION 3.15)

project(ProjectJupiter 
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Game Engine Framework"
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs) instead of static libraries" OFF)
option(BUILD_PROJECTS "Build example projects" ON)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Add subsystems
add_subdirectory(animation)
add_subdirectory(rendering)
add_subdirectory(assets)
add_subdirectory(physics)
add_subdirectory(audio)
add_subdirectory(input)
add_subdirectory(core)

# Add projects if enabled
if(BUILD_PROJECTS)
    add_subdirectory(projects)
endif()
