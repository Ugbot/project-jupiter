# Minimal PBR Demo Project
# Back-to-basics PBR implementation using reference shaders

cmake_minimum_required(VERSION 3.15)

project(minimal-pbr)

# Find Vulkan (required)
find_package(Vulkan)

if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan not found. Minimal PBR demo will not be built.")
    return()
endif()

# Create executable
add_executable(minimal-pbr
    src/main.cpp
)

# Link against engine libraries
target_link_libraries(minimal-pbr
    PRIVATE
        platform
        math
        logging
        rendering
        assets
)

# Include directories
target_include_directories(minimal-pbr
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Find glslangValidator for shader compilation
find_program(GLSLANG_VALIDATOR glslangValidator)

if(GLSLANG_VALIDATOR)
    # Shader source files
    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    set(SHADER_VERT ${SHADER_DIR}/pbr.vert)
    set(SHADER_FRAG ${SHADER_DIR}/pbr.frag)

    # Output SPV files
    set(SHADER_VERT_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/pbr.vert.spv)
    set(SHADER_FRAG_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/pbr.frag.spv)

    # Compile vertex shader
    add_custom_command(
        OUTPUT ${SHADER_VERT_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_VERT} -o ${SHADER_VERT_SPV}
        DEPENDS ${SHADER_VERT}
        COMMENT "Compiling PBR vertex shader to SPIR-V"
        VERBATIM
    )

    # Engine shader includes directory
    set(ENGINE_SHADER_INCLUDES ${CMAKE_SOURCE_DIR}/rendering/shaders/includes)

    # Compile fragment shader with engine includes
    add_custom_command(
        OUTPUT ${SHADER_FRAG_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_FRAG} -o ${SHADER_FRAG_SPV} -I${ENGINE_SHADER_INCLUDES}
        DEPENDS ${SHADER_FRAG}
                ${ENGINE_SHADER_INCLUDES}/constants.glsl
                ${ENGINE_SHADER_INCLUDES}/pbr_functions.glsl
                ${ENGINE_SHADER_INCLUDES}/tonemap.glsl
                ${ENGINE_SHADER_INCLUDES}/normal_mapping.glsl
        COMMENT "Compiling PBR fragment shader to SPIR-V (with engine includes)"
        VERBATIM
    )

    # Custom target for shader compilation
    add_custom_target(minimal_pbr_shaders
        DEPENDS ${SHADER_VERT_SPV} ${SHADER_FRAG_SPV}
        COMMENT "Building Minimal PBR shaders"
    )

    # Make executable depend on shaders
    add_dependencies(minimal-pbr minimal_pbr_shaders)

    # Copy compiled shaders to output directory
    add_custom_command(TARGET minimal-pbr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:minimal-pbr>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SHADER_VERT_SPV}
            $<TARGET_FILE_DIR:minimal-pbr>/shaders/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SHADER_FRAG_SPV}
            $<TARGET_FILE_DIR:minimal-pbr>/shaders/
        COMMENT "Copying PBR shaders to output directory"
    )
else()
    message(WARNING "glslangValidator not found. Minimal PBR demo will not compile shaders.")
endif()

# Set C++ standard
target_compile_features(minimal-pbr PRIVATE cxx_std_20)

# Enable logging
target_compile_definitions(minimal-pbr PRIVATE JUPITER_ENABLE_LOGGING)

# Enable warnings
if(MSVC)
    target_compile_options(minimal-pbr PRIVATE /W4)
else()
    target_compile_options(minimal-pbr PRIVATE -Wall -Wextra -Wpedantic)
endif()
