add_executable(profiling_smoke
    profiling_smoke.cpp
)

target_link_libraries(profiling_smoke
    PRIVATE
        core
        profiling
)

add_test(NAME profiling_smoke COMMAND profiling_smoke)

add_executable(profiling_noop
    profiling_noop.cpp
)

target_compile_definitions(profiling_noop
    PRIVATE
        JUPITER_ENABLE_TRACY=0
)

target_include_directories(profiling_noop
    PRIVATE
        ${CMAKE_SOURCE_DIR}/profiling/include
)

add_test(NAME profiling_noop COMMAND profiling_noop)

add_executable(profiling_threaded
    profiling_threaded.cpp
)

target_link_libraries(profiling_threaded
    PRIVATE
        profiling
        core
)

add_test(NAME profiling_threaded COMMAND profiling_threaded)

# PBR push constants flag/override tests (header-only, no Vulkan needed)
add_executable(pbr_push_constants_test
    pbr_push_constants_test.cpp
)

target_include_directories(pbr_push_constants_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/rendering/include
)

add_test(NAME pbr_push_constants_test COMMAND pbr_push_constants_test)

