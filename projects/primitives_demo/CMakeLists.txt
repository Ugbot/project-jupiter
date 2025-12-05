# Primitives Demo - Showcase procedural geometry shapes
# Demonstrates spawning cubes, spheres, cylinders, planes, and capsules

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

