# Shader Includes
# 
# This directory contains modular GLSL include files that can be used
# by other shaders via #include <includes/filename.glsl>
#
# To compile shaders with includes, use:
#   glslangValidator -V shader.frag -o shader.frag.spv -I${SHADER_INCLUDE_DIR}
#
# Where SHADER_INCLUDE_DIR points to the parent of this directory.

# Set up the include directory path for other CMake targets
set(JUPITER_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. CACHE PATH "Jupiter shader include directory")

# List of shader include files (for reference, not compiled)
set(SHADER_INCLUDES
    constants.glsl
    pbr_functions.glsl
    radiance.glsl
    ambient.glsl
    shadow.glsl
    poisson.glsl
    tonemap.glsl
    normal_mapping.glsl
    camera_ubo.glsl
    light_ubo.glsl
    material_ubo.glsl
)

# Create a custom target to track include file changes
add_custom_target(shader_includes
    SOURCES ${SHADER_INCLUDES}
    COMMENT "Shader include files"
)

# Export the include directory for other shader targets
set(SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. PARENT_SCOPE)

message(STATUS "Shader include directory: ${CMAKE_CURRENT_SOURCE_DIR}/..")

