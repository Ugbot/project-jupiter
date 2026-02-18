# Uber shader compilation
# 
# Single-source shaders for all backends:
#   - SPIR-V for Vulkan (native)
#   - SPIR-V -> MSL via SPIRV-Cross for Metal (runtime)
#   - SPIR-V -> GLSL via SPIRV-Cross for OpenGL (runtime)

# Find shader compiler
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
        "/usr/bin"
        "/usr/local/bin"
        "/opt/homebrew/bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found. Uber shaders will not be compiled.")
    return()
endif()

message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

# Include directory for shared shader code
set(SHADER_INCLUDES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../includes)

# Output directory - use CMAKE_BINARY_DIR/shaders for consistency with other shaders
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/uber)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Uber shader sources
set(UBER_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/uber.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/uber.frag
)

# Compile each shader
set(COMPILED_SHADERS)
foreach(SHADER_SOURCE ${UBER_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT} -I${SHADER_INCLUDES_DIR}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling uber shader: ${SHADER_NAME}"
        VERBATIM
    )
    
    list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
endforeach()

# Create custom target - note ALL to build by default
add_custom_target(uber_shaders ALL
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling uber shaders"
)

# Copy to bin/shaders for runtime access
add_custom_command(TARGET uber_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders/uber
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMPILED_SHADERS} ${CMAKE_BINARY_DIR}/bin/shaders/uber/
    COMMENT "Copying uber shaders to bin/shaders/uber"
)

message(STATUS "Uber shaders configured: ${UBER_SHADER_SOURCES}")
