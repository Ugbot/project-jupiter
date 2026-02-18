# Backend Implementation Priority

## Strategy: "Near Common Denominator" + Native Metal

### Core Philosophy

**Design for modern GPUs** (Vulkan 1.2+, Metal 2+, DX12):
- Target features these share (compute, indirect draw, MRT, storage buffers)
- Don't cripple API for OpenGL 2.0 compatibility
- Provide graceful fallbacks for older hardware

**Native backends, not translation layers:**
- Metal on macOS (NOT MoltenVK)
- Vulkan on Linux/Windows
- OpenGL as universal fallback

## Implementation Order

### Phase 1: Vulkan Backend (Baseline)
**Priority:** High  
**Platforms:** Linux, Windows, (MoltenVK testing on Mac)  
**Status:** Partially exists, needs GHI refactor

**Why first:**
- Already have Vulkan code
- Cross-platform development
- Broadest desktop support

### Phase 2: Metal Backend (macOS Production)
**Priority:** **HIGHEST** (your development platform)  
**Platforms:** macOS, iOS  
**Status:** New implementation

**Why second:**
- Native macOS performance
- No MoltenVK overhead
- Apple-specific optimizations (TBDR, tile memory)
- Better debugging (Xcode)
- Your primary dev platform

**Key point:** Metal is NOT a "nice to have" - it's the PRIMARY macOS backend

### Phase 3: OpenGL Backend (Fallback)
**Priority:** Medium  
**Platforms:** Older hardware, Linux without Vulkan  
**Status:** New implementation (can copy Venus patterns)

**Why third:**
- Fallback only
- Validates GHI abstraction
- Compatibility for older systems

**Limitations accepted:**
- No compute → grass uses CPU
- No indirect → more draw calls
- Slower but functional

### Phase 4: DX12 Backend (Future)
**Priority:** Low  
**Platforms:** Windows native optimization  
**Status:** Future work

**Why later:**
- Vulkan works fine on Windows
- DX12 is optimization, not necessity
- Can wait until Vulkan/Metal proven

## Feature Support by Backend

### Always Available (Tier 1)
- Basic indexed drawing
- Textures + mipmaps
- Depth testing
- MRT
- SimplePipeline
- PBRPipeline (basic)
- DeferredPipeline

### Near-Common (Tier 2) - Vulkan/Metal/DX12
- Compute shaders → GPU grass, trails
- Indirect draw → single draw call rendering
- Storage buffers → GPU-driven culling
- ClusteredPipeline
- VoxelPipeline
- PBRPipeline with IBL

### Backend-Specific (Tier 3+)
- Subgroups (Vulkan/Metal) → faster IBL
- Tile shaders (Metal) → optimized deferred
- Memoryless textures (Metal) → G-Buffer memory savings
- Ray tracing (Vulkan RTX, Metal 3) → reflections, GI

## Development Workflow

### On macOS (your setup)
1. Implement in Vulkan first
2. Test via MoltenVK (validates Vulkan code)
3. Port to Metal (production path)
4. Production builds use Metal

### On Linux/Windows
- Use Vulkan directly
- Metal code paths inactive

### Testing
- CI/CD tests all backends
- MoltenVK validates Vulkan on Mac
- Native Metal validates macOS production path

## Why Not MoltenVK for Production?

**MoltenVK overhead:**
- SPIR-V → MSL translation at runtime
- Vulkan → Metal API translation
- ~10-20% performance loss
- Descriptor set emulation complexity

**Native Metal advantages:**
- Direct Metal API (no translation)
- Compiler optimizations
- Tile-based rendering hints
- Memoryless texture support
- Better Xcode integration
- Future Metal 3+ features

**MoltenVK value:**
- Testing Vulkan code on Mac hardware
- Validating cross-platform Vulkan correctness
- Quick prototyping

## Current Landscape Demo Plan

**Immediate (this session):**
1. Get SimplePipeline working on Vulkan
2. See if geometry appears (fixes blue screen issue)

**Next session:**
3. Implement Metal backend
4. Port SimplePipeline to Metal
5. Compare Vulkan (MoltenVK) vs Metal performance

**Goal:**
- Landscape demo working on native Metal
- Grass/trails using Metal compute
- Full PBR + IBL
- 60+ FPS on M3 Pro

This strategy gets you:
- Best macOS performance (native Metal)
- Cross-platform support (Vulkan)
- Graceful degradation (OpenGL)
- No compromises for legacy hardware

