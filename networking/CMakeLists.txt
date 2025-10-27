add_library(networking
    src/networking.cpp
)

target_include_directories(networking
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(networking PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/networking/networking.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(networking
        PRIVATE NETWORKING_EXPORTS
        INTERFACE NETWORKING_IMPORTS
    )
endif()
