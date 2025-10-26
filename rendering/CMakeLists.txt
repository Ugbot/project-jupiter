add_library(rendering
    src/rendering.cpp
)

target_include_directories(rendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(rendering PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/rendering/rendering.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(rendering
        PRIVATE RENDERING_EXPORTS
        INTERFACE RENDERING_IMPORTS
    )
endif()
