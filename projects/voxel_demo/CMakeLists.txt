# Voxel Demo - Large-scale voxel world rendering demo

add_executable(voxel_demo
    src/main.cpp
)

target_link_libraries(voxel_demo
    PRIVATE
        rendering
        jupiter_voxel
        assets
        math
        logging
        platform
)

# Enable logging
target_compile_definitions(voxel_demo PRIVATE JUPITER_ENABLE_LOGGING)

# =============================================================================
# Shader Compilation
# =============================================================================

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
        "/usr/bin"
        "/usr/local/bin"
        "/opt/homebrew/bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found - voxel_demo shaders will not be compiled")
else()
    message(STATUS "Found glslangValidator for voxel_demo: ${GLSLANG_VALIDATOR}")

    # Shader source files (use shared voxel shaders from rendering module)
    set(VOXEL_SHADER_DIR ${CMAKE_SOURCE_DIR}/rendering/shaders/voxel)
    set(VOXEL_SHADER_SOURCES
        ${VOXEL_SHADER_DIR}/voxel_simple.vert
        ${VOXEL_SHADER_DIR}/voxel_simple.frag
    )

    # Output directory
    set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/shaders/voxel)
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

    # Compile each shader
    set(COMPILED_VOXEL_SHADERS)
    foreach(SHADER_SOURCE ${VOXEL_SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
        set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling voxel shader ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND COMPILED_VOXEL_SHADERS ${SHADER_OUTPUT})
    endforeach()

    # Custom target for voxel shaders
    add_custom_target(voxel_demo_shaders ALL
        DEPENDS ${COMPILED_VOXEL_SHADERS}
        COMMENT "Compiling voxel_demo shaders"
    )

    add_dependencies(voxel_demo voxel_demo_shaders)
    message(STATUS "Voxel demo shaders configured: ${VOXEL_SHADER_SOURCES}")

    # Smooth terrain shaders
    set(SMOOTH_SHADER_SOURCES
        ${VOXEL_SHADER_DIR}/smooth_terrain.vert
        ${VOXEL_SHADER_DIR}/smooth_terrain.frag
    )

    set(COMPILED_SMOOTH_SHADERS)
    foreach(SHADER_SOURCE ${SMOOTH_SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
        set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling smooth terrain shader ${SHADER_NAME}"
            VERBATIM
        )

        list(APPEND COMPILED_SMOOTH_SHADERS ${SHADER_OUTPUT})
    endforeach()

    add_custom_target(smooth_world_shaders ALL
        DEPENDS ${COMPILED_SMOOTH_SHADERS}
        COMMENT "Compiling smooth world shaders"
    )
    message(STATUS "Smooth terrain shaders configured: ${SMOOTH_SHADER_SOURCES}")
endif()

set_target_properties(voxel_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# =============================================================================
# Visual Voxel Demo - Renders voxel terrain with Vulkan
# =============================================================================

add_executable(voxel_visual_demo
    src/voxel_visual_main.cpp
)

target_include_directories(voxel_visual_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/rendering/src  # For vulkan_backend.h
)

target_link_libraries(voxel_visual_demo
    PRIVATE
        rendering
        jupiter_voxel
        assets
        math
        logging
        platform
)

# Enable logging
target_compile_definitions(voxel_visual_demo PRIVATE JUPITER_ENABLE_LOGGING)

if(GLSLANG_VALIDATOR)
    add_dependencies(voxel_visual_demo voxel_demo_shaders)
endif()

set_target_properties(voxel_visual_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# =============================================================================
# LOD Voxel Demo - VisTree-based LOD system
# =============================================================================

add_executable(voxel_lod_demo
    src/voxel_lod_main.cpp
)

target_include_directories(voxel_lod_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/rendering/src  # For vulkan_backend.h
)

target_link_libraries(voxel_lod_demo
    PRIVATE
        rendering
        jupiter_voxel
        assets
        math
        logging
        platform
)

# Enable logging
target_compile_definitions(voxel_lod_demo PRIVATE JUPITER_ENABLE_LOGGING)

if(GLSLANG_VALIDATOR)
    add_dependencies(voxel_lod_demo voxel_demo_shaders)
endif()

set_target_properties(voxel_lod_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# =============================================================================
# Smooth World Demo - Marching Cubes + Perlin caves
# =============================================================================

add_executable(smooth_world_demo
    src/smooth_world_main.cpp
)

target_include_directories(smooth_world_demo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/rendering/src  # For vulkan_backend.h
)

target_link_libraries(smooth_world_demo
    PRIVATE
        rendering
        jupiter_voxel
        assets
        math
        logging
        platform
)

# Enable logging
target_compile_definitions(smooth_world_demo PRIVATE JUPITER_ENABLE_LOGGING)

if(GLSLANG_VALIDATOR)
    add_dependencies(smooth_world_demo smooth_world_shaders)
endif()

set_target_properties(smooth_world_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
