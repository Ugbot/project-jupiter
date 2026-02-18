# Deferred Rendering Demo - Showcasing deferred rendering pipeline

add_executable(deferred_demo
    src/main.cpp
)

target_link_libraries(deferred_demo
    PRIVATE
        rendering
        assets
        math
        logging
        platform
)

# Copy models to build directory (reuse lighting_demo's DamagedHelmet)
add_custom_command(TARGET deferred_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/projects/lighting_demo/models
        ${CMAKE_BINARY_DIR}/bin/models
    COMMENT "Copying DamagedHelmet model to build directory"
)

# Copy assets symlink (if using hellovulkan assets)
add_custom_command(TARGET deferred_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_SOURCE_DIR}/vendored/hellovulkan/Assets
        ${CMAKE_BINARY_DIR}/bin/Assets
    COMMENT "Creating symlink to hellovulkan assets"
)

set_target_properties(deferred_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
