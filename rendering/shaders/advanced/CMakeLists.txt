# Compile advanced rendering shaders to SPIR-V

find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found - advanced shaders will not be compiled")
    return()
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/shaders)

# List of all advanced shaders
set(ADVANCED_SHADERS
    # Shadow mapping
    ${SHADER_SOURCE_DIR}/shadow/depth.vert
    ${SHADER_SOURCE_DIR}/shadow/depth.frag
    # G-buffer
    ${SHADER_SOURCE_DIR}/gbuffer/gbuffer.vert
    ${SHADER_SOURCE_DIR}/gbuffer/gbuffer.frag
    # Deferred lighting
    ${SHADER_SOURCE_DIR}/deferred/lighting.vert
    ${SHADER_SOURCE_DIR}/deferred/lighting.frag
    # SSAO
    ${SHADER_SOURCE_DIR}/ssao/ssao.frag
    # Common
    ${SHADER_SOURCE_DIR}/common/fullscreen.vert
    # Tonemap
    ${SHADER_SOURCE_DIR}/tonemap/tonemap.frag
    # Skybox
    ${SHADER_SOURCE_DIR}/skybox/skybox.vert
    ${SHADER_SOURCE_DIR}/skybox/skybox.frag
    # Enhanced PBR
    ${SHADER_SOURCE_DIR}/pbr/pbr_enhanced.vert
    ${SHADER_SOURCE_DIR}/pbr/pbr_enhanced.frag
)

# Create output directories
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/shadow)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/gbuffer)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/deferred)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/ssao)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/common)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/tonemap)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/skybox)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR}/pbr)

set(SPIRV_OUTPUTS)

foreach(SHADER ${ADVANCED_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    get_filename_component(SHADER_DIR ${SHADER} DIRECTORY)
    get_filename_component(SHADER_SUBDIR ${SHADER_DIR} NAME)
    
    set(SPIRV_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_SUBDIR}/${SHADER_NAME}.spv)
    
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_SUBDIR}/${SHADER_NAME} to SPIR-V"
    )
    
    list(APPEND SPIRV_OUTPUTS ${SPIRV_OUTPUT})
endforeach()

add_custom_target(advanced_shaders ALL DEPENDS ${SPIRV_OUTPUTS})

# Make rendering depend on shader compilation
add_dependencies(rendering advanced_shaders)

message(STATUS "Advanced rendering shaders configured for compilation")

