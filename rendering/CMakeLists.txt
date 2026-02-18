# Rendering module - Uses SDL3 via platform abstraction layer
add_library(rendering
    src/rendering.cpp
    src/vulkan_backend.cpp
    src/application.cpp
    src/lighting.cpp
    src/texture.cpp
    src/ibl_resources.cpp
    src/pipeline_ibl.cpp
    src/vulkan_compute_pipeline.cpp
    # src/gltf_model.cpp  # Deprecated - use assets/gltf_loader.h instead
    src/vulkan_mesh.cpp
    src/material.cpp
    src/material_system.cpp
    src/scene_manager.cpp
    src/render_globals.cpp
    src/default_textures.cpp
    src/primitives.cpp
    # GHI/RAL Multi-Backend System
    src/ghi/ghi_core.cpp
    src/ghi/util/ghi_shader_cross.cpp
    src/ghi/backends/ghi_vulkan.cpp  # Wrapper backend (for existing Application)
    # OpenGL 4.3+ fallback - disabled on macOS (limited to OpenGL 4.1 and deprecated)
    $<$<NOT:$<PLATFORM_ID:Darwin>>:src/ghi/backends/ghi_opengl.cpp>
    #src/ghi/backends/ghi_vulkan_standalone.cpp  # Standalone backend (for new demos) - WIP
    src/ral/ral.cpp
    src/ral/trail_field.cpp
    src/ral/grass_system.cpp
    src/ral/ibl_processor.cpp
    src/pipelines/pipeline_simple.cpp
    src/pipelines/pipeline_pbr.cpp
    # Clustered forward rendering
    src/resources_clustered.cpp
    src/pipeline_base.cpp
    src/pipeline_clustered.cpp
    # Advanced application with ECS integration
    src/application_advanced.cpp
    # Advanced rendering features (shadows, SSAO, tonemap, skybox)
    src/resources_shadow.cpp
    src/resources_gbuffer.cpp
    src/resources_hdr.cpp
    src/pipeline_shadow.cpp
    src/pipeline_gbuffer.cpp
    src/pipeline_deferred.cpp
    src/pipeline_ssao.cpp
    src/pipeline_tonemap.cpp
    src/pipeline_skybox.cpp
    src/application_features.cpp
    # Auto-exposure system
    src/resources_exposure.cpp
    src/pipeline_exposure.cpp
    # ECS-Renderer bridge (Arrow-inspired, decoupled threading)
    src/ecs_bridge.cpp
    # GPU-Driven Rendering (Phase 2)
    src/indirect_draw_buffer.cpp
    src/pipeline_frustum_culling.cpp
    # UI System (Phase 3)
    src/pipeline_imgui.cpp
    src/pipeline_debug.cpp
    # Voxel rendering pipeline
    src/pipeline_voxel.cpp
    src/pipeline_smooth_terrain.cpp
    src/chunk_mesh_pool.cpp
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
        # Include VMA (Vulkan Memory Allocator)
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored>
        # Include stb for image loading
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/stb>
        # Include TinyGLTF for GLTF loading
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/tinygltf>
        # Include GLM for math
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glm>
)

# Link against platform abstraction (which provides SDL3 windowing)
target_link_libraries(rendering
    PUBLIC
        platform    # Provides SDL3 windowing and platform utilities
        assets      # Asset loading and management (CPU-only)
        math
        logging
        profiling   # Tracy profiling integration
        jupiter_ecs # ECS with Arrow-style kernels
        imgui       # Dear ImGui with SDL3+Vulkan backend
        imguizmo    # ImGuizmo for transform gizmos
)

# SPIRV-Cross for shader cross-compilation
if(EXISTS ${CMAKE_SOURCE_DIR}/vendored/spirv-cross/CMakeLists.txt)
    add_subdirectory(${CMAKE_SOURCE_DIR}/vendored/spirv-cross spirv-cross EXCLUDE_FROM_ALL)
    target_link_libraries(rendering PRIVATE
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-msl
        spirv-cross-hlsl
    )
    target_include_directories(rendering PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/spirv-cross
    )
endif()

# metal-cpp for native Metal backend (macOS only)
if(APPLE)
    target_sources(rendering PRIVATE
        src/ghi/backends/ghi_metal_complete.cpp
    )
    
    target_include_directories(rendering PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/metal-cpp
    )
    
    target_link_libraries(rendering PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
    )
    
    target_compile_definitions(rendering PRIVATE
        GHI_METAL_AVAILABLE
    )
endif()

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

# Add shader includes (modular GLSL library)
add_subdirectory(shaders/includes)

# Add IBL shaders compilation
add_subdirectory(shaders/ibl)

# Add clustered forward shaders compilation
add_subdirectory(shaders/clustered)

# Add advanced feature shaders compilation (shadow, gbuffer, ssao, tonemap, skybox)
add_subdirectory(shaders/advanced)

# Add auto-exposure compute shaders compilation
add_subdirectory(shaders/exposure)

# Add GPU-driven compute shaders (frustum culling, etc.)
add_subdirectory(shaders/compute)

# Add foliage shaders compilation (grass, vegetation)
add_subdirectory(shaders/foliage)

# Add voxel shaders compilation
add_subdirectory(shaders/voxel)

# Add uber shaders compilation (single-source for all backends)
add_subdirectory(shaders/uber)

# Add PBR shaders compilation (Cook-Torrance forward PBR)
add_subdirectory(shaders/pbr)

# Add ImGui overlay shaders compilation
add_subdirectory(shaders/imgui)