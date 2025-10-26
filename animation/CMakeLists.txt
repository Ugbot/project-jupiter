add_library(animation
    src/animation.cpp
)

target_include_directories(animation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(animation PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/animation/animation.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(animation
        PRIVATE ANIMATION_EXPORTS
        INTERFACE ANIMATION_IMPORTS
    )
endif()
