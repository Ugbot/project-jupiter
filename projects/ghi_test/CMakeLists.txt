# GHI Test - Basic forward renderer demo
# Tests GHI Metal backend with SimplePipeline

add_executable(ghi_test
    src/main.cpp
)

target_link_libraries(ghi_test PRIVATE
    rendering
    logging
    platform
    math
)

# Output to bin directory
set_target_properties(ghi_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# macOS specific
if(APPLE)
    set_target_properties(ghi_test PROPERTIES
        MACOSX_BUNDLE FALSE
    )
endif()

