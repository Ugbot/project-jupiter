add_library(assets
    src/assets.cpp
)

target_include_directories(assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(assets PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/assets/assets.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(assets
        PRIVATE ASSETS_EXPORTS
        INTERFACE ASSETS_IMPORTS
    )
endif()
