# Meshoptimizer library
# https://github.com/zeux/meshoptimizer

add_library(meshoptimizer STATIC
    allocator.cpp
    clusterizer.cpp
    indexanalyzer.cpp
    indexcodec.cpp
    indexgenerator.cpp
    overdrawoptimizer.cpp
    partition.cpp
    quantization.cpp
    rasterizer.cpp
    simplifier.cpp
    spatialorder.cpp
    stripifier.cpp
    vcacheoptimizer.cpp
    vertexcodec.cpp
    vertexfilter.cpp
    vfetchoptimizer.cpp
)

target_include_directories(meshoptimizer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Enable optimizations for Release builds
if(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(meshoptimizer PRIVATE -O3)
endif()



