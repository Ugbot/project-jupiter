add_library(audio
    src/audio.cpp
)

target_include_directories(audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Set library properties
set_target_properties(audio PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/audio/audio.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(audio
        PRIVATE AUDIO_EXPORTS
        INTERFACE AUDIO_IMPORTS
    )
endif()
