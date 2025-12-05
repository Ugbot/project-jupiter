# Spinning Cube Demo - Demonstrates 3D rotation and projection
cmake_minimum_required(VERSION 3.15)

# Find glslangValidator for shader compilation
find_program(GLSL_VALIDATOR glslangValidator HINTS
    $ENV{VULKAN_SDK}/bin
    $ENV{VULKAN_SDK}/Bin
    $ENV{VULKAN_SDK}/macOS/bin
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendored/vulkan/base/macOS/bin
)

if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found! Please install Vulkan SDK or check VULKAN_SDK environment variable")
endif()

message(STATUS "Found glslangValidator: ${GLSL_VALIDATOR}")

# Compile shaders to SPIR-V
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/bin/shaders")

# Create shader output directory
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

# Vertex shader
set(SHADER_VERT_SOURCE "${SHADER_SOURCE_DIR}/cube.vert")
set(SHADER_VERT_SPV "${SHADER_BINARY_DIR}/cube.vert.spv")

# Fragment shader
set(SHADER_FRAG_SOURCE "${SHADER_SOURCE_DIR}/cube.frag")
set(SHADER_FRAG_SPV "${SHADER_BINARY_DIR}/cube.frag.spv")

# Compile vertex shader
add_custom_command(
    OUTPUT ${SHADER_VERT_SPV}
    COMMAND ${GLSL_VALIDATOR} -V ${SHADER_VERT_SOURCE} -o ${SHADER_VERT_SPV}
    DEPENDS ${SHADER_VERT_SOURCE}
    COMMENT "Compiling vertex shader: cube.vert"
)

# Compile fragment shader
add_custom_command(
    OUTPUT ${SHADER_FRAG_SPV}
    COMMAND ${GLSL_VALIDATOR} -V ${SHADER_FRAG_SOURCE} -o ${SHADER_FRAG_SPV}
    DEPENDS ${SHADER_FRAG_SOURCE}
    COMMENT "Compiling fragment shader: cube.frag"
)

# Custom target for building shaders
add_custom_target(spinning_cube_shaders
    DEPENDS ${SHADER_VERT_SPV} ${SHADER_FRAG_SPV}
    COMMENT "Building Vulkan shaders for spinning cube"
)

# Create executable
add_executable(spinning_cube
    src/main.cpp
)

# Link against rendering library (which provides Application framework)
target_link_libraries(spinning_cube
    PRIVATE
        rendering
        logging
        math
)

# Ensure shaders are compiled before building the executable
add_dependencies(spinning_cube spinning_cube_shaders)

# Copy shaders to binary directory after build
add_custom_command(TARGET spinning_cube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Shaders for spinning cube ready in ${SHADER_BINARY_DIR}"
    COMMENT "Spinning cube shaders compiled"
)
