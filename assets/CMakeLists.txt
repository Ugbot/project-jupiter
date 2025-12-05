# SQLite source
set(SQLITE_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/sqlite/sqlite3.c
)

# Platform-specific file watcher
if(APPLE)
    set(FILE_WATCHER_SOURCE src/file_watcher_macos.cpp)
elseif(UNIX)
    set(FILE_WATCHER_SOURCE src/file_watcher_linux.cpp)  # TODO: Implement
elseif(WIN32)
    set(FILE_WATCHER_SOURCE src/file_watcher_windows.cpp)  # TODO: Implement
endif()

add_library(assets
    src/assets.cpp
    src/asset_database.cpp
    src/shader_loader.cpp
    src/gltf_loader.cpp
    src/hdr_loader.cpp
    ${FILE_WATCHER_SOURCE}
    ${SQLITE_SOURCE}
)

target_include_directories(assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/glm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/sqlite
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendored/tinygltf
)

# Link dependencies
target_link_libraries(assets
    PUBLIC
        platform
        memory
        logging
        SDL3-static
)

# Platform-specific frameworks
if(APPLE)
    target_link_libraries(assets PRIVATE "-framework CoreServices")
endif()

# Set library properties
set_target_properties(assets PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/assets/assets.h
)

# Enable logging
target_compile_definitions(assets PRIVATE JUPITER_ENABLE_LOGGING)

# Platform-specific settings for DLL export
if(BUILD_SHARED_LIBS)
    target_compile_definitions(assets
        PRIVATE ASSETS_EXPORTS
        INTERFACE ASSETS_IMPORTS
    )
endif()
