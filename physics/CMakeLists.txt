add_library(physics
    src/physics.cpp
)

target_include_directories(physics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(physics PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/physics/physics.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(physics
        PRIVATE PHYSICS_EXPORTS
        INTERFACE PHYSICS_IMPORTS
    )
endif()
