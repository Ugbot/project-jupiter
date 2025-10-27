add_library(logging
    src/logging.cpp
)

target_include_directories(logging
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(logging PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/logging/logging.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(logging
        PRIVATE LOGGING_EXPORTS
        INTERFACE LOGGING_IMPORTS
    )
endif()
