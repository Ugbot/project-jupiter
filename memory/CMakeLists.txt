add_library(memory
    src/memory.cpp
)

target_include_directories(memory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(memory PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/memory/memory.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(memory
        PRIVATE MEMORY_EXPORTS
        INTERFACE MEMORY_IMPORTS
    )
endif()
