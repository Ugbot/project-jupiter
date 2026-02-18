# Landscape Demo - Grassy outdoor terrain with GPU-generated grass and player trails
# Demonstrates heightmap terrain, compute-based grass instancing, and trail system

add_executable(landscape_demo
    src/main.cpp
    src/terrain_heightmap.cpp
    src/trail_field.cpp
    src/grass_system.cpp  # Stub implementation - needs GHI compute support
)

target_link_libraries(landscape_demo PRIVATE
    rendering
    platform
    assets
    logging
    math
    input
)

# Include GLM for noise and math
target_include_directories(landscape_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/glm
)

# Depend on shader compilation targets
add_dependencies(landscape_demo compute_shaders foliage_shaders advanced_shaders)

# Copy PBR shaders (reuse from lighting_demo or build output)
add_custom_command(TARGET landscape_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/bin/shaders
    $<TARGET_FILE_DIR:landscape_demo>/shaders
    COMMENT "Copying shaders for landscape demo"
)

# Set output directory
set_target_properties(landscape_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# macOS settings
if(APPLE)
    set_target_properties(landscape_demo PROPERTIES
        MACOSX_BUNDLE FALSE
    )
endif()

# ============================================================================
# GHI/RAL Version (new cross-platform demo)
# ============================================================================

add_executable(landscape_demo_ghi
    src/main_ghi.cpp
)

target_link_libraries(landscape_demo_ghi PRIVATE
    rendering
    platform
    logging
    math
)

target_include_directories(landscape_demo_ghi
    PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/glm
)

add_dependencies(landscape_demo_ghi compute_shaders foliage_shaders advanced_shaders)

add_custom_command(TARGET landscape_demo_ghi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/bin/shaders
    $<TARGET_FILE_DIR:landscape_demo_ghi>/shaders
    COMMENT "Copying shaders for landscape demo (GHI)"
)

set_target_properties(landscape_demo_ghi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(APPLE)
    set_target_properties(landscape_demo_ghi PROPERTIES
        MACOSX_BUNDLE FALSE
    )
endif()

