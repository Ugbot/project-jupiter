add_library(profiling INTERFACE)

target_include_directories(profiling
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Propagate instrumentation toggles to all dependents
target_compile_definitions(profiling
    INTERFACE
        JUPITER_ENABLE_TRACY=$<BOOL:${JUPITER_ENABLE_TRACY}>
)

if(JUPITER_ENABLE_TRACY)
    # Ensure user code sees Tracy instrumentation macros and links the client
    target_compile_definitions(profiling
        INTERFACE
            TRACY_ENABLE=1
            TRACY_ON_DEMAND=1
    )
    if(NOT DEFINED TRACY_PUBLIC_INCLUDE_DIR)
        set(TRACY_PUBLIC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendored/tracy/public")
    endif()
    target_include_directories(profiling
        INTERFACE
            ${TRACY_PUBLIC_INCLUDE_DIR}
    )
    target_link_libraries(profiling
        INTERFACE
            Tracy::TracyClient
    )
endif()

