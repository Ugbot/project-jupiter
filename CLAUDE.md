# Project Jupiter - Game Engine Architecture Guide

## Project Overview

Project Jupiter is a high-performance, platform-agnostic game engine built with modern C++20 and Vulkan. The engine is designed as a scaffold that allows users to build games in the `projects/` folder with minimal CMake configuration.

## Core Architectural Principles

### Platform Agnostic
- All platform-specific code must be isolated in the `platform/` module
- Use abstraction layers for OS-specific functionality (windowing, file I/O, threading, etc.)
- Never assume a specific platform in core engine code

### Lock-Free Design
- Prefer lock-free data structures and algorithms
- Use atomic operations and memory ordering guarantees
- Avoid mutexes and locks wherever possible
- When synchronization is required, document why and consider lock-free alternatives

### Memory Management
- **NO runtime allocations during gameplay/render loops**
- Use object pools for frequently allocated/deallocated objects
- Use ring buffers for streaming data and temporary allocations
- Pre-allocate all resources during initialization
- Memory allocators should be configurable per-module
- `new`, `delete`, `malloc`, and `free` are expensive - avoid them in hot paths

### Performance-First
- Code must be written for correct high performance
- Design for easy SIMD vectorization (data-oriented design)
- Use structure-of-arrays (SoA) over array-of-structures (AoS) where appropriate
- Cache-friendly data layouts are mandatory
- Profile before optimizing, but write performant code from the start

### Event Sourcing & CQRS
- Use event sourcing patterns for state management where appropriate
- Separate command and query responsibilities (CQRS)
- Events are immutable and should be stored in ring buffers
- Event handlers should be registered at initialization, not runtime

## Technology Stack

- **Language**: C++20 (use modern features: concepts, ranges, modules where beneficial)
- **Graphics API**: Vulkan (platform-agnostic, explicit control)
- **Build System**: CMake (modern CMake 3.x+)
- **Testing**: Randomized inputs to avoid hardcoded happy paths

## Build System Guidelines

### CMake Structure
- Each module is a CMake target (library)
- Games in `projects/` should easily integrate with minimal CMake setup
- Users should be able to build from any directory
- Support out-of-source builds
- Provide clear target dependencies

### Module Organization
```
module_name/
├── CMakeLists.txt
├── include/module_name/
│   └── public_headers.h
└── src/
    └── implementation.cpp
```

## Module Guidelines

### Core Modules
- **assets**: Asset loading and management (textures, audio, models)
- **rendering**: Vulkan rendering abstraction
- **event_system**: Lock-free event distribution
- **logging**: Thread-safe, low-overhead logging
- **math**: SIMD-optimized math library
- **memory**: Custom allocators, object pools, ring buffers
- **networking**: Platform-agnostic networking
- **platform**: OS-specific abstractions
- **scripting**: Scripting language integration
- **ui**: User interface rendering
- **utils**: General utilities

## Coding Standards

### C++20 Usage
- Use `std::span` for non-owning views of contiguous data
- Use concepts for template constraints
- Use `constexpr` and `consteval` for compile-time computation
- Use structured bindings for clarity
- Use `std::atomic` with explicit memory ordering

### Data-Oriented Design
- Think in terms of data transformations, not objects
- Group related data together for cache efficiency
- Separate hot and cold data
- Use plain old data (POD) types where possible

### SIMD Considerations
- Align data to 16/32/64-byte boundaries where needed
- Use `alignas()` specifier
- Structure data for vectorization opportunities
- Avoid branches in hot loops

## Testing Philosophy

- **Always randomize test inputs** to ensure no hardcoded happy paths
- Test edge cases and boundary conditions
- Benchmark performance-critical code
- Write tests before removing old implementations
- Commit when tests pass and milestones are reached

## Development Workflow

### Before Making Changes
- Understand the module's purpose and dependencies
- Check if changes align with lock-free/no-allocation principles
- Consider SIMD implications of data layout changes

### During Development
- Get new code working before removing old implementations
- If tests pass at a milestone, do a git commit
- Never remove features without asking
- Don't simplify without understanding - implement correctly in smaller parts

### Git Workflow
- **Always ask before performing git actions**
- Commit when reaching stable milestones
- Write clear, descriptive commit messages
- Keep commits atomic and focused

## Anti-Patterns to Avoid

- Runtime memory allocations in hot paths
- Using locks/mutexes when lock-free alternatives exist
- Platform-specific code outside `platform/` module
- Hardcoded test values (randomize inputs)
- Removing working features to "simplify"
- Over-abstraction that hinders performance

## Vulkan Best Practices

- Use explicit synchronization (barriers, semaphores, fences)
- Minimize state changes
- Batch draw calls
- Use descriptor sets efficiently
- Pre-bake pipeline state objects
- Leverage async compute where beneficial

## Dependencies

- **GLFW**: Windowing and input (vendored)
- **Vulkan SDK**: Graphics API (vendored)
- Other dependencies should be vendored in `vendored/` directory

## Questions to Ask Before Implementation

1. Does this allocate memory at runtime?
2. Is this lock-free?
3. Can this be vectorized?
4. Is the data layout cache-friendly?
5. Is this platform-agnostic?
6. Does this fit the event sourcing model?
7. Are we removing features we might need?

---

**Remember**: Correctness first, then performance. But design for performance from the start.
- save all plans into docs so we dont loose them. check them as we go and update/check off things
- run visual tests for at least 10 seconds after they finish loadign because otherwise its not really a test
- the game engine has to run headless so none of the core systems can depend on the renderer.