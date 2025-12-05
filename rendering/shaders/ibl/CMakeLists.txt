# IBL shader compilation

# Find shader compiler
find_program(GLSL_VALIDATOR glslangValidator)

if(NOT GLSL_VALIDATOR)
    message(WARNING "glslangValidator not found. IBL shaders will not be compiled.")
    return()
endif()

message(STATUS "Found glslangValidator: ${GLSL_VALIDATOR}")

# Output directory for compiled shaders
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# List of shaders to compile
set(COMPUTE_SHADERS
    brdf_lut.comp
)

set(VERTEX_SHADERS
    fullscreen.vert
)

set(FRAGMENT_SHADERS
    equirect_to_cube.frag
    irradiance_convolution.frag
    prefilter_specular.frag
)

# Compile compute shaders
foreach(SHADER ${COMPUTE_SHADERS})
    set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSL_VALIDATOR} -V ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        COMMENT "Compiling IBL compute shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

# Compile vertex shaders
foreach(SHADER ${VERTEX_SHADERS})
    set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSL_VALIDATOR} -V ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        COMMENT "Compiling IBL vertex shader: ${SHADER}"
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
        COMMENT "Compiling IBL fragment shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

# Create custom target for shader compilation
add_custom_target(ibl_shaders
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling all IBL shaders"
)

# Install compiled shaders to build/bin/shaders/ibl/
install(FILES ${COMPILED_SHADERS}
    DESTINATION bin/shaders/ibl
)

# Copy compiled shaders to build directory
add_custom_command(TARGET ibl_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders/ibl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMPILED_SHADERS} ${CMAKE_BINARY_DIR}/bin/shaders/ibl/
    COMMENT "Copying compiled IBL shaders to build directory"
)
