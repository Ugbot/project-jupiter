# Shapes Demo - Simple animated shapes on Metal and Vulkan
# Tests GHI/RAL rendering pipeline with clear visual feedback

add_executable(shapes_demo
    src/main.cpp
)

target_link_libraries(shapes_demo PRIVATE
    rendering
    platform
    logging
    math
)

target_include_directories(shapes_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/glm
)

# Depend on shader compilation
add_dependencies(shapes_demo advanced_shaders)

# Copy shaders
add_custom_command(TARGET shapes_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/bin/shaders
    $<TARGET_FILE_DIR:shapes_demo>/shaders
    COMMENT "Copying shaders for shapes demo"
)

set_target_properties(shapes_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(APPLE)
    set_target_properties(shapes_demo PROPERTIES
        MACOSX_BUNDLE FALSE
    )
endif()
