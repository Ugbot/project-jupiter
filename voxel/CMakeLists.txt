# Voxel Module - Large-scale voxel world system
# Based on Oryol stb_voxel_render integration + vkguide Ascendant GPU-driven patterns

add_library(jupiter_voxel STATIC
    src/chunk_pool.cpp
    src/chunk_map.cpp
    src/chunk_entity_bridge.cpp
    src/dirty_queue.cpp
    src/mesh_buffer_pool.cpp
    src/streaming_manager.cpp
    src/vis_tree.cpp
    src/voxel_job_system.cpp
    src/voxel_mesher.cpp
    src/voxel_mesh_converter.cpp
    src/voxel_physics_mesh.cpp
    src/voxel_world.cpp
)

target_include_directories(jupiter_voxel
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# GLM for math types
target_include_directories(jupiter_voxel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glm>
)

# Rendering module for vertex formats and chunk_mesh_pool
target_include_directories(jupiter_voxel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../rendering/include>
        # Assets module (needed by vulkan_mesh.h)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../assets/include>
        # Vulkan headers (needed for vertex_formats.h, chunk_mesh_pool.h)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external/vulkan>
        # VMA (Vulkan Memory Allocator) for VmaAllocator
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored>
        # ECS module for entity buffer and types
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../ecs/include>
)

# stb_voxel_render.h location (from oryol-samples)
target_include_directories(jupiter_voxel
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/stb
)

# Find threading library
find_package(Threads REQUIRED)

# Link dependencies
target_link_libraries(jupiter_voxel
    PUBLIC
        memory
        jupiter_ecs
        Threads::Threads
    PRIVATE
        logging
)

# C++20 required
target_compile_features(jupiter_voxel PUBLIC cxx_std_20)

# Alias for cleaner usage
add_library(jupiter::voxel ALIAS jupiter_voxel)

# Install headers
install(DIRECTORY include/voxel
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(TARGETS jupiter_voxel
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
