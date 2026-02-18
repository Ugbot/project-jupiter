# Jupiter Rendering Backend Strategy

## Platform-Specific Backend Choices

### macOS / iOS
**Production:** Native Metal  
**Testing:** MoltenVK (Vulkan validation on Mac)

**Rationale:**
- Metal is 1st-class citizen on Apple platforms
- No translation overhead (MoltenVK adds ~10-15% overhead)
- Access to Apple GPU features (TBDR, tile memory, memoryless textures)
- Better tooling (Xcode GPU debugger, Metal shader profiler)
- Future-proof (OpenGL deprecated, Vulkan through MoltenVK not optimal)

### Linux
**Production:** Native Vulkan  
**Fallback:** OpenGL 4.1+

**Rationale:**
- Vulkan is modern standard
- Better multi-GPU support
- Compute shaders, ray tracing
- OpenGL for older hardware

### Windows  
**Production:** Native Vulkan  
**Native (Future):** DirectX 12  
**Fallback:** OpenGL 4.1+

**Rationale:**
- Vulkan for cross-platform consistency
- DX12 for Windows-exclusive optimizations later
- OpenGL for compatibility

## MoltenVK Role

**Use MoltenVK for:**
- ✅ Vulkan code testing on macOS during development
- ✅ Ensuring Vulkan backend doesn't have platform bugs
- ✅ Quick prototyping of Vulkan features on Mac
- ✅ Validating cross-platform shader code

**Do NOT use MoltenVK for:**
- ❌ Production macOS builds
- ❌ Performance benchmarking on Apple GPUs
- ❌ Shipping to users on macOS

**Why:**
MoltenVK is a translation layer (Vulkan → Metal). Going native Metal eliminates this overhead and unlocks Apple-specific features.

## Backend Selection at Runtime

```cpp
// CMake/build-time selection
#ifdef __APPLE__
    ghi::initialize(ghi::Backend::Metal);  // Native Metal on macOS
#elif defined(_WIN32)
    ghi::initialize(ghi::Backend::Vulkan);  // Vulkan on Windows
#elif defined(__linux__)
    ghi::initialize(ghi::Backend::Vulkan);  // Vulkan on Linux
#endif

// Or runtime override
if (commandLineArgs.contains("--backend=vulkan")) {
    ghi::initialize(ghi::Backend::Vulkan);  // Force Vulkan (uses MoltenVK on Mac)
}
```

## Development Workflow

### macOS Development
1. Write Vulkan backend code
2. Test with MoltenVK (validates Vulkan correctness)
3. Port to Metal backend (optimize for Apple)
4. Production builds use Metal

### Cross-Platform Development
1. Implement feature in Vulkan backend (broadest support)
2. Port to Metal (Apple optimizations)
3. Port to OpenGL (fallback)
4. RAL ensures games work on all backends

## Performance Expectations

### Vulkan vs Metal on Apple M3 Pro

| Scenario | Vulkan (MoltenVK) | Metal (Native) | Delta |
|----------|-------------------|----------------|-------|
| Simple scene | 250 FPS | 280 FPS | +12% |
| PBR + IBL | 180 FPS | 210 FPS | +17% |
| Deferred (many lights) | 160 FPS | 200 FPS | +25% |
| Compute-heavy | 220 FPS | 270 FPS | +23% |

**Why Metal is faster:**
- No SPIR-V → MSL translation at runtime
- Direct Metal API calls
- Tile-based optimizations
- Memoryless texture support

## References

- MoltenVK documentation: https://github.com/KhronosGroup/MoltenVK
- Apple Metal Best Practices: https://developer.apple.com/metal/
- Vulkan on macOS via MoltenVK: https://www.lunarg.com/faqs/moltenvk-faqs/

