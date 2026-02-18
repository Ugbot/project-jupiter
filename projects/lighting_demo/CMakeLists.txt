# Lighting Demo - Forward rendering with hot-reload shaders

add_executable(lighting_demo
    src/main.cpp
)

target_link_libraries(lighting_demo
    PRIVATE
        rendering
        assets
        math
        logging
        platform
)

# =============================================================================
# Shader Compilation
# =============================================================================

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
    message(WARNING "glslangValidator not found - lighting_demo shaders will not be compiled")
else()
    message(STATUS "Found glslangValidator for lighting_demo: ${GLSLANG_VALIDATOR}")

    # Shader source files
    set(LIGHTING_DEMO_SHADER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.frag
    )
    
    # Output directory for compiled shaders
    set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
    
    # Compile each shader with proper dependencies
    set(COMPILED_SHADERS)
    foreach(SHADER_SOURCE ${LIGHTING_DEMO_SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
        set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
        
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling shader ${SHADER_NAME}"
            VERBATIM
        )
        
        list(APPEND COMPILED_SHADERS ${SHADER_OUTPUT})
    endforeach()
    
    # Custom target for lighting_demo shaders
    add_custom_target(lighting_demo_shaders ALL
        DEPENDS ${COMPILED_SHADERS}
        COMMENT "Compiling lighting_demo shaders"
    )
    
    # Ensure demo depends on compiled shaders
    add_dependencies(lighting_demo lighting_demo_shaders)
    
    message(STATUS "Lighting demo shaders configured: ${LIGHTING_DEMO_SHADER_SOURCES}")
endif()

# =============================================================================
# Also copy shaders to lighting_demo subdirectory (for organization)
# =============================================================================

add_custom_command(TARGET lighting_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/bin/shaders/lighting_demo
    COMMENT "Copying lighting_demo source shaders to build directory"
)

# Copy models to build directory
add_custom_command(TARGET lighting_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/models
        ${CMAKE_BINARY_DIR}/bin/models
    COMMENT "Copying DamagedHelmet model to build directory"
)

# Copy assets symlink (if using hellovulkan assets)
add_custom_command(TARGET lighting_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/../../vendored/hellovulkan/Assets
        ${CMAKE_BINARY_DIR}/bin/Assets
    COMMENT "Creating symlink to hellovulkan assets"
)

set_target_properties(lighting_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
