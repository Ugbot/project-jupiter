# Deferred rendering shaders

set(DEFERRED_SHADERS
    lighting.vert
    lighting.frag
)

foreach(SHADER ${DEFERRED_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(SHADER_OUTPUT ${CMAKE_BINARY_DIR}/shaders/deferred/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/shaders/deferred
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -I${CMAKE_CURRENT_SOURCE_DIR}/../ ${SHADER_INPUT} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_INPUT}
        COMMENT "Compiling ${SHADER_NAME}"
    )

    list(APPEND DEFERRED_SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(deferred_shaders ALL DEPENDS ${DEFERRED_SHADER_OUTPUTS})
