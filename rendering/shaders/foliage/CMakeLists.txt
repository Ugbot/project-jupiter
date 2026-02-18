# Foliage shaders compilation (grass, vegetation, etc.)

# Find shader compiler
find_program(GLSL_VALIDATOR glslangValidator)

if(NOT GLSL_VALIDATOR)
    message(WARNING "glslangValidator not found. Foliage shaders will not be compiled.")
    return()
endif()

message(STATUS "Found glslangValidator for foliage shaders: ${GLSL_VALIDATOR}")

# Output directory for compiled shaders
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/shaders/foliage)

# Shader include directory (for #include directives)
set(SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../)

# List of foliage shaders to compile
set(FOLIAGE_SHADERS
    grass.vert
    grass.frag
)

# Compile foliage shaders
foreach(SHADER ${FOLIAGE_SHADERS})
    set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER}.spv)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${GLSL_VALIDATOR} --target-env vulkan1.1 -V -I${SHADER_INCLUDE_DIR} ${INPUT_FILE} -o ${OUTPUT_FILE}
        DEPENDS ${INPUT_FILE}
        COMMENT "Compiling foliage shader: ${SHADER}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

# Create custom target for shader compilation
add_custom_target(foliage_shaders
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling all foliage shaders"
)

# Install compiled shaders
install(FILES ${COMPILED_SHADERS}
    DESTINATION bin/shaders/foliage
)

