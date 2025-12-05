add_library(platform
    src/platform.cpp
    src/sdl_wrapper.cpp
    src/window_sdl.cpp
)

target_include_directories(platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # Include vendored Vulkan headers for SDL_vulkan.h support
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/base>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external/vulkan>
)

# Link against SDL3 static library
target_link_libraries(platform
    PUBLIC
        SDL3::SDL3-static
)

# Platform-specific includes and libraries
if(WIN32)
    target_compile_definitions(platform PRIVATE PLATFORM_WINDOWS)
elseif(APPLE)
    target_compile_definitions(platform PRIVATE PLATFORM_MACOS)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(COCOA_LIBRARY Cocoa)
    target_link_libraries(platform PRIVATE ${COREFOUNDATION_LIBRARY} ${COCOA_LIBRARY})
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(platform PRIVATE PLATFORM_LINUX)
    target_link_libraries(platform PRIVATE dl pthread)
endif()

# Set library properties
set_target_properties(platform PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/platform/platform.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(platform
        PRIVATE PLATFORM_EXPORTS
        INTERFACE PLATFORM_IMPORTS
    )
endif()
