# Clustered Forward Shaders Compilation
# Compiles GLSL shaders to SPIR-V using glslangValidator

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
    message(WARNING "glslangValidator not found - clustered shaders will not be compiled")
    return()
endif()

message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

# Shader source files
set(CLUSTERED_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/aabb_generator.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/light_culling.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/frustum_cull.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/pbr_clustered.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/pbr_clustered.frag
)

# Output directory for compiled shaders
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/clustered)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Include directory for shared shader code
set(SHADER_INCLUDES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../includes)

# Compile each shader
set(COMPILED_SHADERS)
foreach(SHADER_SOURCE ${CLUSTERED_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT} -I${SHADER_INCLUDES_DIR}
        DEPENDS ${SHADER_SOURCE}
                ${SHADER_INCLUDES_DIR}/constants.glsl
                ${SHADER_INCLUDES_DIR}/pbr_functions.glsl
                ${SHADER_INCLUDES_DIR}/tonemap.glsl
        COMMENT "Compiling shader ${SHADER_NAME} (with includes)"
        VERBATIM
    )
    
    list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
endforeach()

# Custom target for all clustered shaders
add_custom_target(clustered_shaders ALL
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling clustered forward shaders"
)

# Copy to bin directory for runtime use
add_custom_command(TARGET clustered_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADER_OUTPUT_DIR}
        ${CMAKE_BINARY_DIR}/bin/shaders/clustered
    COMMENT "Copying clustered shaders to bin directory"
)

message(STATUS "Clustered shaders configured: ${CLUSTERED_SHADER_SOURCES}")

