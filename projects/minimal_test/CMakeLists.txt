# Minimal rendering test - absolute bare bones
add_executable(minimal_test
    src/main.cpp
)

target_link_libraries(minimal_test PRIVATE
    rendering
    platform
    logging
)

set_target_properties(minimal_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

