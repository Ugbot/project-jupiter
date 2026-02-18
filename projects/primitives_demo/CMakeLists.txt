# Primitives Demo - Showcase procedural geometry shapes
# Demonstrates spawning cubes, spheres, cylinders, planes, and capsules

# Original Vulkan-only demo (uses Application class)
add_executable(primitives_demo
    src/main.cpp
)

target_link_libraries(primitives_demo PRIVATE
    rendering
    platform
    assets
    logging
    math
    input
)

# Copy shaders from lighting_demo (uses same PBR pipeline)
add_custom_command(TARGET primitives_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/../lighting_demo/shaders
    $<TARGET_FILE_DIR:primitives_demo>/shaders
)

# Cross-platform GHI demo (works on Metal, Vulkan, OpenGL)
add_executable(primitives_demo_ghi
    src/main_ghi.cpp
    src/imgui_overlay_ghi.cpp
)

target_include_directories(primitives_demo_ghi PRIVATE
    ${CMAKE_SOURCE_DIR}/vendored/glm
)

target_link_libraries(primitives_demo_ghi PRIVATE
    rendering
    platform
    logging
    SDL3::SDL3-static
)

# Depend on shader compilation
add_dependencies(primitives_demo_ghi uber_shaders pbr_shaders imgui_shaders)

# Copy compiled shaders from build directory (not source)
add_custom_command(TARGET primitives_demo_ghi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:primitives_demo_ghi>/shaders/uber
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/uber/uber.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/uber/uber.frag.spv
        $<TARGET_FILE_DIR:primitives_demo_ghi>/shaders/uber/
    COMMENT "Copying compiled uber shaders to demo"
)

# Copy compiled PBR shaders from build directory (not source)
add_custom_command(TARGET primitives_demo_ghi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:primitives_demo_ghi>/shaders/pbr
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/pbr/pbr.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/pbr/pbr.frag.spv
        $<TARGET_FILE_DIR:primitives_demo_ghi>/shaders/pbr/
    COMMENT "Copying compiled PBR shaders to demo"
)

# Copy compiled ImGui shaders from build directory (not source)
add_custom_command(TARGET primitives_demo_ghi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:primitives_demo_ghi>/shaders/imgui
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/shaders/imgui/imgui.vert.spv
        ${CMAKE_BINARY_DIR}/shaders/imgui/imgui.frag.spv
        $<TARGET_FILE_DIR:primitives_demo_ghi>/shaders/imgui/
    COMMENT "Copying compiled ImGui shaders to demo"
)
