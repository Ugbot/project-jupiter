# ECS Module - Arrow-style columnar storage with vectorized kernels
# Double-buffered design for multi-reader concurrency

add_library(jupiter_ecs STATIC
    src/kernel_registry.cpp
    src/simd_ops.cpp
    src/world.cpp
    src/builtin_kernels.cpp
    src/simulation_runner.cpp
)

target_include_directories(jupiter_ecs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# GLM is header-only, add include path directly
target_include_directories(jupiter_ecs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glm>
)

# Link dependencies (memory module for queues if needed)
target_link_libraries(jupiter_ecs
    PRIVATE
        memory
)

# C++17 required for std::optional, structured bindings, etc.
target_compile_features(jupiter_ecs PUBLIC cxx_std_17)

# SIMD flags for optimized builds - architecture-dependent
include(CheckCXXCompilerFlag)

# Detect architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    # ARM64 (Apple Silicon, etc.) - use NEON
    target_compile_definitions(jupiter_ecs PRIVATE JUPITER_HAS_NEON=1)
    message(STATUS "ECS: Using ARM NEON SIMD")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x86")
    # x86/x64 - use AVX2 if available
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
        if(COMPILER_SUPPORTS_AVX2)
            option(JUPITER_ECS_AVX2 "Enable AVX2 intrinsics" ON)
            if(JUPITER_ECS_AVX2)
                target_compile_options(jupiter_ecs PRIVATE -mavx2 -mfma)
                target_compile_definitions(jupiter_ecs PRIVATE JUPITER_HAS_AVX2=1)
                message(STATUS "ECS: Using x86 AVX2 SIMD")
            endif()
        endif()
        
        target_compile_options(jupiter_ecs PRIVATE
            $<$<CONFIG:Release>:-march=native>
            $<$<CONFIG:RelWithDebInfo>:-march=native>
        )
    elseif(MSVC)
        target_compile_options(jupiter_ecs PRIVATE
            $<$<CONFIG:Release>:/arch:AVX2>
            $<$<CONFIG:RelWithDebInfo>:/arch:AVX2>
        )
        target_compile_definitions(jupiter_ecs PRIVATE JUPITER_HAS_AVX2=1)
        message(STATUS "ECS: Using x86 AVX2 SIMD (MSVC)")
    endif()
else()
    message(STATUS "ECS: Using scalar fallback (unknown architecture)")
endif()

# Platform-specific threading
if(UNIX AND NOT APPLE)
    target_link_libraries(jupiter_ecs PRIVATE pthread)
endif()

# Alias for cleaner usage
add_library(jupiter::ecs ALIAS jupiter_ecs)

# Install headers
install(DIRECTORY include/ecs
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(TARGETS jupiter_ecs
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

