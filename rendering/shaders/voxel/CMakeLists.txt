# Voxel shader compilation

# Find shader compiler
find_program(GLSL_VALIDATOR glslangValidator)

if(NOT GLSL_VALIDATOR)
    message(WARNING "glslangValidator not found. Voxel shaders will not be compiled.")
    return()
endif()

message(STATUS "Compiling voxel shaders with: ${GLSL_VALIDATOR}")

# Output directory for compiled shaders
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# List of shaders to compile
set(VERTEX_SHADERS
    voxel_simple.vert
    smooth_terrain.vert
)

set(FRAGMENT_SHADERS
    voxel_simple.frag
    voxel_shadow.frag
    smooth_terrain.frag
)

# Compile vertex shaders
foreach(SHADER ${VERTEX_SHADERS})
    set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSL_VALIDATOR} -V ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        COMMENT "Compiling voxel vertex shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

# Compile fragment shaders
foreach(SHADER ${FRAGMENT_SHADERS})
    set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSL_VALIDATOR} -V ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        COMMENT "Compiling voxel fragment shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

# Create custom target for shader compilation
add_custom_target(voxel_shaders
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling all voxel shaders"
)

# Install compiled shaders to build/bin/shaders/voxel/
install(FILES ${COMPILED_SHADERS}
    DESTINATION bin/shaders/voxel
)

# Copy compiled shaders to build directory
add_custom_command(TARGET voxel_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders/voxel
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMPILED_SHADERS} ${CMAKE_BINARY_DIR}/bin/shaders/voxel/
    COMMENT "Copying compiled voxel shaders to build directory"
)
