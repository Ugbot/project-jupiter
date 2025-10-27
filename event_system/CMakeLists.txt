add_library(event_system
    src/event_system.cpp
)

target_include_directories(event_system
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(event_system PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/event_system/event_system.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(event_system
        PRIVATE EVENT_SYSTEM_EXPORTS
        INTERFACE EVENT_SYSTEM_IMPORTS
    )
endif()
