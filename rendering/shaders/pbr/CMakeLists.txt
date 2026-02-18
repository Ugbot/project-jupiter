# PBR shader compilation
#
# Compiles GLSL shaders to SPIR-V using glslangValidator.
# Output:
#   - ${CMAKE_BINARY_DIR}/shaders/pbr/*.spv
#   - copied to ${CMAKE_BINARY_DIR}/bin/shaders/pbr/*.spv for runtime

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
        "/usr/bin"
        "/usr/local/bin"
        "/opt/homebrew/bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found. PBR shaders will not be compiled.")
    return()
endif()

message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

set(SHADER_INCLUDES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../includes)

set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/pbr)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(PBR_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/pbr.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/pbr.frag
)

set(COMPILED_SHADERS)
foreach(SHADER_SOURCE ${PBR_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT} -I${SHADER_INCLUDES_DIR}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling PBR shader: ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
endforeach()

add_custom_target(pbr_shaders ALL
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling PBR shaders"
)

add_custom_command(TARGET pbr_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders/pbr
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMPILED_SHADERS} ${CMAKE_BINARY_DIR}/bin/shaders/pbr/
    COMMENT "Copying PBR shaders to bin/shaders/pbr"
)

message(STATUS "PBR shaders configured: ${PBR_SHADER_SOURCES}")




