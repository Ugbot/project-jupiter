# Auto-exposure shader compilation

# Find shader compiler
find_program(GLSL_VALIDATOR glslangValidator)

if(NOT GLSL_VALIDATOR)
    message(WARNING "glslangValidator not found. Exposure shaders will not be compiled.")
    return()
endif()

message(STATUS "Found glslangValidator for exposure shaders: ${GLSL_VALIDATOR}")

# Output directory for compiled shaders
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# List of compute shaders to compile
set(COMPUTE_SHADERS
    luminance_histogram.comp
    histogram_average.comp
    exposure_adapt.comp
)

# Compile compute shaders
foreach(SHADER ${COMPUTE_SHADERS})
    set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${GLSL_VALIDATOR} -V ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        COMMENT "Compiling exposure compute shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

# Create custom target for shader compilation
add_custom_target(exposure_shaders
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling all exposure shaders"
)

# Install compiled shaders to build/bin/shaders/exposure/
install(FILES ${COMPILED_SHADERS}
    DESTINATION bin/shaders/exposure
)

# Copy compiled shaders to build directory
add_custom_command(TARGET exposure_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders/exposure
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMPILED_SHADERS} ${CMAKE_BINARY_DIR}/bin/shaders/exposure/
    COMMENT "Copying compiled exposure shaders to build directory"
)

