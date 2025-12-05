# Base PBR - Minimal PBR rendering reference

add_executable(base-pbr
    src/main.cpp
)

target_link_libraries(base-pbr
    PRIVATE
        rendering
        assets
        math
        logging
        platform
)

# Find glslangValidator for shader compilation
find_program(GLSLANG_VALIDATOR glslangValidator)

if(GLSLANG_VALIDATOR)
    # Shader directories
    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    set(ENGINE_SHADER_INCLUDES ${CMAKE_SOURCE_DIR}/rendering/shaders/includes)
    set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/shaders/base-pbr)

    # Compile vertex shader
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_DIR}/pbr.vert.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_DIR}/pbr.vert -o ${SHADER_OUTPUT_DIR}/pbr.vert.spv
        DEPENDS ${SHADER_DIR}/pbr.vert
        COMMENT "Compiling base-pbr vertex shader"
        VERBATIM
    )

    # Compile fragment shader with engine includes
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_DIR}/pbr.frag.spv
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_DIR}/pbr.frag -o ${SHADER_OUTPUT_DIR}/pbr.frag.spv -I${ENGINE_SHADER_INCLUDES}
        DEPENDS ${SHADER_DIR}/pbr.frag
                ${ENGINE_SHADER_INCLUDES}/constants.glsl
                ${ENGINE_SHADER_INCLUDES}/pbr_functions.glsl
                ${ENGINE_SHADER_INCLUDES}/tonemap.glsl
        COMMENT "Compiling base-pbr fragment shader (with engine includes)"
        VERBATIM
    )

    # Custom target for shader compilation
    add_custom_target(base_pbr_shaders
        DEPENDS 
            ${SHADER_OUTPUT_DIR}/pbr.vert.spv 
            ${SHADER_OUTPUT_DIR}/pbr.frag.spv
        COMMENT "Building base-pbr shaders"
    )

    add_dependencies(base-pbr base_pbr_shaders)
else()
    # Fallback: just copy shaders (pre-compiled)
    add_custom_command(TARGET base-pbr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            ${CMAKE_BINARY_DIR}/bin/shaders/base-pbr
        COMMENT "Copying base-pbr shaders to build directory"
    )
endif()

# Share models with lighting_demo
add_custom_command(TARGET base-pbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../lighting_demo/models
        ${CMAKE_BINARY_DIR}/bin/models
    COMMENT "Copying models to build directory"
)

# Share assets symlink (hellovulkan IBL resources)
add_custom_command(TARGET base-pbr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/../../vendored/hellovulkan/Assets
        ${CMAKE_BINARY_DIR}/bin/Assets
    COMMENT "Creating symlink to hellovulkan assets"
)

set_target_properties(base-pbr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
