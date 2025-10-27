# Build GLFW as static library for windowing
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glfw glfw_build)

add_library(rendering
    src/rendering.cpp
    src/vulkan_backend.cpp
    src/application.cpp
)

target_include_directories(rendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # Include vendored Vulkan headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/base>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external/vulkan>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/external/ktx/include>
        # Include GLFW for windowing
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glfw/include>
)

# Link against platform abstraction for windowing
target_link_libraries(rendering
    PUBLIC
        platform
        math
        logging
        glfw      # Windowing library
)

# Find Vulkan SDK (with MoltenVK support for macOS)
find_package(Vulkan QUIET)

if(Vulkan_FOUND)
    target_include_directories(rendering PUBLIC ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(rendering PUBLIC ${Vulkan_LIBRARIES})
    target_compile_definitions(rendering PRIVATE VULKAN_SDK_AVAILABLE)

    # Platform-specific Vulkan settings
    if(APPLE)
        # Enable MoltenVK support
        target_compile_definitions(rendering PRIVATE VK_USE_PLATFORM_METAL_EXT)
        message(STATUS "Using Vulkan with MoltenVK on macOS")
    elseif(WIN32)
        target_compile_definitions(rendering PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    elseif(UNIX AND NOT APPLE)
        # Linux - use XCB or Wayland
        find_package(XCB QUIET)
        if(XCB_FOUND)
            target_compile_definitions(rendering PRIVATE VK_USE_PLATFORM_XCB_KHR)
            target_link_libraries(rendering PUBLIC ${XCB_LIBRARIES})
        endif()
    endif()

else()
    # Try to find MoltenVK directly on macOS
    if(APPLE)
        find_library(MOLTENVK_LIBRARY NAMES MoltenVK
                     HINTS "$ENV{VULKAN_SDK}/lib" "$ENV{VULKAN_SDK}/dylib/macOS"
                     "${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/apple/MoltenVK.xcframework"
                     PATHS "/usr/local/lib")
        if(MOLTENVK_LIBRARY)
            target_link_libraries(rendering PUBLIC ${MOLTENVK_LIBRARY})
            target_compile_definitions(rendering PRIVATE VULKAN_SDK_AVAILABLE VK_USE_PLATFORM_METAL_EXT)
            message(STATUS "Found MoltenVK library directly")
            set(Vulkan_FOUND ON)
        else()
            # Try to link the framework directly
            find_library(MOLTENVK_FRAMEWORK MoltenVK
                         PATHS "$ENV{VULKAN_SDK}/macOS/lib/MoltenVK.xcframework"
                               "${CMAKE_CURRENT_SOURCE_DIR}/../vendored/vulkan/apple/MoltenVK.xcframework")
            if(MOLTENVK_FRAMEWORK)
                target_link_libraries(rendering PUBLIC "-framework MoltenVK")
                target_compile_definitions(rendering PRIVATE VULKAN_SDK_AVAILABLE VK_USE_PLATFORM_METAL_EXT)
                message(STATUS "Using MoltenVK framework")
                set(Vulkan_FOUND ON)
            else()
                message(WARNING "Neither Vulkan SDK nor MoltenVK found. Rendering will use software fallback.")
                target_compile_definitions(rendering PRIVATE VULKAN_SDK_NOT_AVAILABLE)
            endif()
        endif()
    else()
        message(WARNING "Vulkan SDK not found. Rendering will use software fallback.")
        target_compile_definitions(rendering PRIVATE VULKAN_SDK_NOT_AVAILABLE)
    endif()
endif()

# Set library properties
set_target_properties(rendering PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/rendering/rendering.h
)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(rendering
        PRIVATE RENDERING_EXPORTS
        INTERFACE RENDERING_IMPORTS
    )
endif()

# Compile definitions
target_compile_definitions(rendering
    PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_NO_SMART_HANDLE
        JUPITER_ENABLE_LOGGING
)