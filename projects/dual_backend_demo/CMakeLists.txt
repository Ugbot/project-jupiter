# Dual Backend Demo - Shows GHI working on both Metal and Vulkan
# Usage: ./dual_backend_demo --backend=metal
#        ./dual_backend_demo --backend=vulkan

add_executable(dual_backend_demo
    src/main.cpp
)

target_link_libraries(dual_backend_demo PRIVATE
    rendering
    logging
    platform
    math
)

# Output to bin directory
set_target_properties(dual_backend_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# macOS specific
if(APPLE)
    set_target_properties(dual_backend_demo PROPERTIES
        MACOSX_BUNDLE FALSE
    )
endif()

