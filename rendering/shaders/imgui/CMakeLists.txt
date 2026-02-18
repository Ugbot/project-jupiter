# ImGui shader compilation
#
# Compiles the ImGui overlay shaders to SPIR-V using glslangValidator.
# Output:
#   - ${CMAKE_BINARY_DIR}/shaders/imgui/*.spv
#   - copied to ${CMAKE_BINARY_DIR}/bin/shaders/imgui/*.spv for runtime

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
        "/usr/bin"
        "/usr/local/bin"
        "/opt/homebrew/bin"
)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found. ImGui shaders will not be compiled.")
    return()
endif()

message(STATUS "Found glslangValidator: ${GLSLANG_VALIDATOR}")

set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders/imgui)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(IMGUI_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui.frag
)

set(COMPILED_SHADERS)
foreach(SHADER_SOURCE ${IMGUI_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ImGui shader: ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
endforeach()

add_custom_target(imgui_shaders ALL
    DEPENDS ${COMPILED_SHADERS}
    COMMENT "Compiling ImGui shaders"
)

add_custom_command(TARGET imgui_shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/shaders/imgui
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${COMPILED_SHADERS} ${CMAKE_BINARY_DIR}/bin/shaders/imgui/
    COMMENT "Copying ImGui shaders to bin/shaders/imgui"
)

message(STATUS "ImGui shaders configured: ${IMGUI_SHADER_SOURCES}")




