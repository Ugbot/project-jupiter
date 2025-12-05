add_library(math
    src/math.cpp
)

target_include_directories(math
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glm
)

# Set library properties
set_target_properties(math PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/math/math.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(math
        PRIVATE MATH_EXPORTS
        INTERFACE MATH_IMPORTS
    )
endif()
