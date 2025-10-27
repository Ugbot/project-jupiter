# Vulkan Triangle Demo Project
# Demonstrates the rendering subsystem with a classic Vulkan triangle

cmake_minimum_required(VERSION 3.15)

project(vulkan_triangle)

# Find Vulkan (required for this demo)
find_package(Vulkan)

if(NOT Vulkan_FOUND)
    message(WARNING "Vulkan not found. Vulkan Triangle demo will not be built.")
    return()
endif()

# Create executable
add_executable(vulkan_triangle
    src/main.cpp
)

# Link against engine libraries
target_link_libraries(vulkan_triangle
    PRIVATE
        platform      # Cross-platform utilities
        math          # Vector math
        logging       # Logging system
        rendering     # Vulkan rendering abstraction (now includes windowing)
        assets        # Asset management (for shaders)
)

# Include directories
target_include_directories(vulkan_triangle
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Find glslangValidator for shader compilation
find_program(GLSLANG_VALIDATOR glslangValidator)

if(GLSLANG_VALIDATOR)
    # Compile shaders to SPIR-V
    set(SHADER_VERT_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle.vert.spv)
    set(SHADER_FRAG_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle.frag.spv)

    add_custom_command(
        OUTPUT ${SHADER_VERT_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert -o ${SHADER_VERT_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert
        COMMENT "Compiling vertex shader to SPIR-V"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${SHADER_FRAG_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag -o ${SHADER_FRAG_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag
        COMMENT "Compiling fragment shader to SPIR-V"
        VERBATIM
    )

    # Create a custom target for shader compilation
    add_custom_target(vulkan_triangle_shaders
        DEPENDS ${SHADER_VERT_SPV} ${SHADER_FRAG_SPV}
        COMMENT "Building Vulkan shaders"
    )

    # Make vulkan_triangle depend on shader compilation
    add_dependencies(vulkan_triangle vulkan_triangle_shaders)

    # Copy shaders to build directory (GLSL + SPIR-V)
    add_custom_command(TARGET vulkan_triangle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:vulkan_triangle>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:vulkan_triangle>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle.vert.spv
            $<TARGET_FILE_DIR:vulkan_triangle>/shaders/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle.frag.spv
            $<TARGET_FILE_DIR:vulkan_triangle>/shaders/
        COMMENT "Copying shaders for Vulkan triangle demo"
    )
else()
    # Fallback: just copy GLSL shaders if glslangValidator is not available
    message(WARNING "glslangValidator not found. Vulkan triangle demo will use GLSL shaders directly (not recommended for production).")
    add_custom_command(TARGET vulkan_triangle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:vulkan_triangle>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:vulkan_triangle>/shaders
        COMMENT "Copying shaders for Vulkan triangle demo"
    )
endif()

# Set C++ standard
target_compile_features(vulkan_triangle PRIVATE cxx_std_20)

# Enable logging
target_compile_definitions(vulkan_triangle PRIVATE JUPITER_ENABLE_LOGGING)

# Enable warnings
if(MSVC)
    target_compile_options(vulkan_triangle PRIVATE /W4)
else()
    target_compile_options(vulkan_triangle PRIVATE -Wall -Wextra -Wpedantic)
endif()
