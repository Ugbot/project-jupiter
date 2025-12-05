# Lighting Demo - Forward rendering with hot-reload shaders

add_executable(lighting_demo
    src/main.cpp
)

target_link_libraries(lighting_demo
    PRIVATE
        rendering
        assets
        math
        logging
        platform
)

# Copy shaders to build directory
add_custom_command(TARGET lighting_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/bin/shaders/lighting_demo
    COMMENT "Copying lighting_demo shaders to build directory"
)

# Copy models to build directory
add_custom_command(TARGET lighting_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        ${CMAKE_BINARY_DIR}/bin/models
    COMMENT "Copying DamagedHelmet model to build directory"
)

# Copy assets symlink (if using hellovulkan assets)
add_custom_command(TARGET lighting_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/../../vendored/hellovulkan/Assets
        ${CMAKE_BINARY_DIR}/bin/Assets
    COMMENT "Creating symlink to hellovulkan assets"
)

set_target_properties(lighting_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
