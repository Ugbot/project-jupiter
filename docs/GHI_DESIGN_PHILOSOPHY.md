# GHI Design Philosophy: "Near Common Denominator"

## Core Principle

**Design for modern GPUs, fallback gracefully on older hardware.**

GHI API targets features that:
- Vulkan supports natively
- Metal supports natively  
- DX12 supports natively
- OpenGL 4.3+ supports (with some effort)
- OpenGL 4.1 tries best-effort or skips

**NOT "lowest common denominator"** (don't cripple to OpenGL 2.0)  
**NOT "highest common"** (don't require ray tracing everywhere)  
**"Near common"** (target modern features, fallback older)

---

## Feature Tiers

### Tier 1: Required (All Backends)

**GHI guarantees these work:**
- Basic indexed drawing
- Vertex/index buffers
- Textures with mipmaps
- Depth testing
- Blending, culling
- Uniforms/constant buffers
- MRT (multiple render targets)

**If a backend can't do this, it's not supported.**

### Tier 2: Near-Common (Expected, Fallback OK)

**GHI provides API, backends try to support:**
- Compute shaders
- Indirect draw (GPU-driven)
- Storage buffers (SSBO)
- Instanced rendering
- Cubemap textures
- Dynamic vertex pulling

**Vulkan/Metal/DX12:** ✅ Full support  
**OpenGL 4.3+:** ✅ Full support  
**OpenGL 4.1:** ⚠️ Best-effort or CPU fallback

**Example:** Grass compute shader
- Vulkan/Metal: GPU generates instances
- OpenGL 4.3: GPU generates instances
- OpenGL 4.1: CPU generates instances (slower but works)

### Tier 3: Optional (Capability Check Required)

**GHI exposes capability, pipelines check before using:**
- Subgroups/SIMD (for IBL optimization)
- Tessellation shaders
- Geometry shaders
- Variable rate shading

**Usage pattern:**
```cpp
if (ghi::hasSubgroupOperations()) {
    // Use fast subgroup IBL compute
} else {
    // Use slower but universal IBL compute
}
```

### Tier 4: Backend-Specific (Not in GHI)

**Accessed via backend-specific extensions:**
- Ray tracing
- Mesh shaders
- Tile shaders (Metal)
- DirectStorage (DX12)

**Usage pattern:**
```cpp
#ifdef GHI_BACKEND_METAL
    auto metalExt = ghi::getMetalExtensions();
    metalExt->beginTilePass();  // Metal-only
#endif
```

---

## Pipeline Support Matrix

### SimplePipeline
**Requirements:** Tier 1 only  
**Availability:** ✅ **All backends**  
**Fallback:** N/A (baseline)

### PBRPipeline
**Requirements:** Tier 1 + cubemaps  
**Availability:** ✅ **All backends**  
**Fallback:** Uses simple Lambert if IBL fails  
**Optimization:** Tier 3 (subgroups) for faster IBL

### DeferredPipeline
**Requirements:** Tier 1 + MRT  
**Availability:** ✅ **All backends**  
**Fallback:** N/A (MRT is Tier 1)  
**Optimization:** Tier 4 (Metal tile shaders) for memoryless textures

### ClusteredPipeline
**Requirements:** Tier 2 (compute shaders)  
**Availability:** ✅ Vulkan, Metal, DX12, OpenGL 4.3+  
**Fallback:** → PBRPipeline on OpenGL 4.1

### VoxelPipeline
**Requirements:** Tier 2 (compute, indirect draw)  
**Availability:** ✅ Vulkan, Metal, DX12, OpenGL 4.3+  
**Fallback:** → CPU meshing + SimplePipeline on OpenGL 4.1

### RayTracedPipeline
**Requirements:** Tier 4 (ray tracing)  
**Availability:** ✅ Vulkan (RTX), Metal 3 (M3+), DX12 (DXR)  
**Fallback:** → PBRPipeline

---

## API Design Implications

### GHI API Surface

**INCLUDE in GHI:**
```cpp
// Tier 1 - guaranteed
void draw(uint32_t vertexCount);
void drawIndexed(uint32_t indexCount);
BufferHandle createBuffer(...);
TextureHandle createTexture(...);

// Tier 2 - near-common (fallback if unavailable)
void dispatch(uint32_t x, uint32_t y, uint32_t z);
void drawIndirect(BufferHandle indirectBuffer);
void drawIndexedIndirect(BufferHandle indirectBuffer);
```

**EXCLUDE from GHI (backend-specific):**
```cpp
// NO: void traceRays(...);  // Only some backends
// NO: void beginTilePass(...);  // Metal-only
// NO: void dispatchMesh(...);  // Not in OpenGL

// Instead: Access via extensions
if (backend == Backend::Metal) {
    auto ext = ghi::getMetalExtensions();
    ext->beginTilePass();
}
```

### RAL Pipeline Selection

**Automatic fallback:**
```cpp
// Game requests clustered
ral::usePipeline(ral::Pipeline::Clustered);

// RAL checks backend capability
if (!ghi::hasComputeShaders()) {
    LOG_WARN("Clustered pipeline needs compute, falling back to PBR");
    actualPipeline = ral::Pipeline::PBR;
}
```

**Capability query:**
```cpp
bool ral::pipelineAvailable(Pipeline pipeline) {
    switch (pipeline) {
        case Pipeline::Simple:
        case Pipeline::PBR:
        case Pipeline::Deferred:
            return true;  // Always available (Tier 1)
        
        case Pipeline::Clustered:
        case Pipeline::Voxel:
            return ghi::hasComputeShaders();  // Tier 2
        
        case Pipeline::RayTraced:
            return ghi::hasRayTracing();  // Tier 4
    }
}
```

---

## Backend Capability Examples

### Modern Backends (Target These)

**Vulkan 1.2+, Metal 2+, DX12:**
- ✅ All Tier 1 features
- ✅ All Tier 2 features (compute, indirect, storage buffers)
- ✅ Most Tier 3 features (subgroups)

**Design GHI/RAL for these** - they represent current/future hardware

### Legacy Backend (Best-Effort)

**OpenGL 4.1 (macOS minimum):**
- ✅ All Tier 1
- ⚠️ Partial Tier 2 (no compute, emulated indirect)
- ❌ No Tier 3

**Fallbacks:**
- Clustered → PBR (forward multi-pass)
- Grass compute → CPU instance generation
- Voxel compute → CPU meshing

**Still usable, just slower**

### Ancient Hardware (Not Supported)

**OpenGL 3.3, DX11:**
- We don't design for these
- If someone wants support, they can add a backend
- GHI abstraction makes it possible, but not our priority

---

## Example: Landscape Demo Feature Degradation

### On Vulkan/Metal (Full Feature Set)
```cpp
✅ Terrain rendering (PBRPipeline)
✅ GPU-generated grass (compute shader, 1M instances)
✅ Trail system (compute, ping-pong textures)
✅ IBL skybox lighting (subgroup-optimized)
✅ Indirect draw (single draw call for grass)
✅ Shadows (optional)
```

### On OpenGL 4.3+ (Near-Common)
```cpp
✅ Terrain rendering (PBRPipeline)
✅ GPU-generated grass (compute shader, slower than Vulkan)
✅ Trail system (compute, ping-pong textures)
✅ IBL skybox lighting (no subgroup, uses slower compute)
✅ Indirect draw (via ARB_draw_indirect)
⚠️ Shadows (may need more passes)
```

### On OpenGL 4.1 (Graceful Degradation)
```cpp
✅ Terrain rendering (SimplePipeline, Lambertian not PBR)
⚠️ CPU-generated grass (10K instances max, not 1M)
❌ Trail system disabled (no compute)
✅ Static skybox (no IBL, just cubemap)
✅ Multi-draw (CPU loop, not indirect)
❌ Shadows disabled
```

**Still playable, just lower visual quality and performance**

---

## GHI Capability Queries

```cpp
namespace jupiter::ghi {

struct Capabilities {
    // Tier 1 (always true if backend loaded)
    bool hasIndexedDraw = true;
    bool hasDepthTest = true;
    bool hasMRT = true;
    
    // Tier 2 (check before using)
    bool hasComputeShaders = false;
    bool hasIndirectDraw = false;
    bool hasStorageBuffers = false;
    
    // Tier 3 (optimization only)
    bool hasSubgroups = false;
    bool hasTessellation = false;
    bool hasGeometryShaders = false;
    
    // Tier 4 (backend-specific)
    bool hasRayTracing = false;
    bool hasMeshShaders = false;
    bool hasTileShaders = false;  // Metal-only
    bool hasMemorylessTextures = false;  // Metal-only
    
    // Limits
    uint32_t maxTextureSize = 0;
    uint32_t maxComputeWorkGroupSize[3] = {0, 0, 0};
    uint32_t maxColorAttachments = 0;
};

const Capabilities& getCapabilities();

// Convenience checks
bool hasComputeShaders();
bool hasIndirectDraw();
bool hasSubgroups();

} // namespace ghi
```

**Usage in pipelines:**
```cpp
// ClusteredPipeline constructor
ClusteredPipeline::ClusteredPipeline() {
    if (!ghi::hasComputeShaders()) {
        throw std::runtime_error("ClusteredPipeline requires compute shaders");
    }
    // Proceed with compute-based light culling
}

// PBRPipeline IBL generation
void PBRPipeline::generateIBL() {
    if (ghi::hasSubgroups()) {
        // Fast path: subgroup-optimized (vk-gltf-viewer pattern)
        useSubgroupIBL();
    } else {
        // Slow path: standard compute
        useStandardIBL();
    }
}
```

---

## Rationale: Why "Near Common" Not "Lowest Common"

**If we designed for OpenGL 2.0 (lowest common):**
- ❌ No compute → grass must be CPU (slow)
- ❌ No MRT → deferred needs multiple passes (slow)
- ❌ No storage buffers → GPU-driven impossible
- ❌ Crippled modern features for legacy hardware

**With "near common" (modern GPUs):**
- ✅ Design assumes compute, indirect, MRT
- ✅ Modern techniques work well
- ✅ Old hardware gets simplified version (not broken, just less fancy)
- ✅ 95% of users get full experience

**Target audience:**
- Modern hardware (2020+)
- Apple Silicon (M1+)
- NVIDIA GTX 1060+ / AMD RX 580+
- Anyone with Vulkan 1.2, Metal 2, or DX12

**Not targeting:**
- Intel HD Graphics 4000 (2012)
- Raspberry Pi 3 (GLES 2.0)
- Ancient laptops

If someone needs ancient hardware, OpenGL 4.1 fallback gives basic functionality.

---

## Updated Todo Priority

Since Metal is PRIMARY for macOS (where we're developing):

**High Priority:**
1. GHI Vulkan (baseline, works everywhere)
2. **GHI Metal** (macOS production path)
3. SimplePipeline (both backends)
4. PBRPipeline (both backends)

**Medium Priority:**
5. RAL refinement
6. Landscape demo on Metal
7. Deferred with Metal tile shaders

**Low Priority:**
8. OpenGL backend (fallback only)
9. Advanced pipelines (clustered, ray traced)

**This gets landscape demo working FASTER** - focus on Metal since that's your development platform.


