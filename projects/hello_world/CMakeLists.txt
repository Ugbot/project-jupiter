add_executable(hello_world
    src/main.cpp
)

# Link all subsystem libraries
target_link_libraries(hello_world
    PRIVATE
        core
        animation
        rendering
        assets
        physics
        audio
        input
)
