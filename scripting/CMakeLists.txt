add_library(scripting
    src/scripting.cpp
)

target_include_directories(scripting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(scripting PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/scripting/scripting.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(scripting
        PRIVATE SCRIPTING_EXPORTS
        INTERFACE SCRIPTING_IMPORTS
    )
endif()
