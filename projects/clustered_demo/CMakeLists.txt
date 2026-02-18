# Clustered Forward Demo
# Demonstrates ECS integration and clustered forward shading

add_executable(clustered_demo
    src/main.cpp
)

target_link_libraries(clustered_demo
    PRIVATE
        rendering
        jupiter_ecs
        input
)

# Include GLM for demo code
target_include_directories(clustered_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/glm
)

# Depend on shader targets - clustered_demo uses PBR shaders from lighting_demo
add_dependencies(clustered_demo clustered_shaders)
if(TARGET lighting_demo_shaders)
    add_dependencies(clustered_demo lighting_demo_shaders)
endif()

# Set output directory
set_target_properties(clustered_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# macOS: Set up bundle info if needed
if(APPLE)
    set_target_properties(clustered_demo PROPERTIES
        MACOSX_BUNDLE FALSE
    )
endif()

