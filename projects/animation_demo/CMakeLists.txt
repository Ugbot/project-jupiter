# Animation Demo - Skeletal Animation with GLTF
# Demonstrates loading animated models using the existing PBR pipeline

add_executable(animation_demo
    src/main.cpp
)

target_link_libraries(animation_demo PRIVATE
    rendering
    assets
    animation
    platform
    logging
    math
)

# Create symlink to DancingStormtrooper model
add_custom_command(TARGET animation_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/models
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_SOURCE_DIR}/vendored/hellovulkan/assets/models/DancingStormtrooper01
        ${CMAKE_BINARY_DIR}/bin/models/DancingStormtrooper01
    COMMENT "Creating symlink to DancingStormtrooper model"
)
