# GHI/RAL Multi-Backend Renderer

## Architecture Overview

Jupiter's rendering is being refactored into a **three-layer architecture**:

```
┌─────────────────────────────────┐
│   Application Layer             │  Games use simple API
│   (MyGame, LandscapeDemo, etc.) │
├─────────────────────────────────┤
│   RAL (Render Abstraction)      │  High-level: meshes, materials, pipelines
│   rendering/include/ral/        │
├─────────────────────────────────┤
│   GHI (Graphics Hardware)       │  Low-level: buffers, textures, draw calls
│   rendering/include/ghi/        │
├─────────────────────────────────┤
│   Backends (Vulkan/Metal/GL)    │  Platform-specific GPU code
│   rendering/src/ghi/backends/   │
└─────────────────────────────────┘
```

## What's Implemented

### GHI (Graphics Hardware Interface)

**API Definition** - ✅ Complete
- [`include/rendering/ghi/ghi.h`](include/rendering/ghi/ghi.h) - Public API (~300 lines)
- [`include/rendering/ghi/ghi_types.h`](include/rendering/ghi/ghi_types.h) - Types, enums, handles
- [`include/rendering/ghi/ighi_backend.h`](include/rendering/ghi/ighi_backend.h) - Backend interface

**Core Implementation** - ✅ Complete
- [`src/ghi/ghi_core.cpp`](src/ghi/ghi_core.cpp) - Resource pools, backend dispatch

**Vulkan Backend** - ⚠️ Stub Only
- [`src/ghi/backends/ghi_vulkan.h`](src/ghi/backends/ghi_vulkan.h) - Header
- [`src/ghi/backends/ghi_vulkan.cpp`](src/ghi/backends/ghi_vulkan.cpp) - Stubs (~200 lines)
- **TODO:** Wrap existing VulkanRenderer (~2000 lines needed)

### RAL (Render Abstraction Layer)

**API Definition** - ✅ Complete  
- [`include/rendering/ral/ral.h`](include/rendering/ral/ral.h) - Public API (~200 lines)
- [`include/rendering/ral/ral_types.h`](include/rendering/ral/ral_types.h) - Mesh, Material, Light types

**Implementation** - ❌ Not Started
- Needs `ral_core.cpp`, `ral_mesh.cpp`, `ral_material.cpp`, etc. (~1500 lines)

### Pipelines

**All** - ❌ Not Started
- SimplePipeline, PBRPipeline, DeferredPipeline, etc.

## Usage Examples

### GHI API (Low-Level)

```cpp
#include "rendering/ghi/ghi.h"

// Initialize with backend
ghi::initialize(ghi::Backend::Metal);  // or Vulkan, OpenGL

// Create resources
ghi::BufferHandle vbo = ghi::createBuffer({
    .type = ghi::BufferType::Vertex,
    .size = sizeof(vertices),
    .data = vertices
});

ghi::TextureHandle tex = ghi::createTextureFromFile("texture.png", true);

// Render
ghi::beginFrame();
ghi::beginRenderPass();
ghi::bindVertexBuffer(vbo);
ghi::draw(vertexCount, 1);
ghi::endRenderPass();
ghi::endFrame();
```

### RAL API (High-Level)

```cpp
#include "rendering/ral/ral.h"

// Initialize
ral::initialize();
ral::usePipeline(ral::Pipeline::PBR);

// Create scene objects
auto mesh = ral::createCube(10.0f);
auto material = ral::createPBRMaterial(
    glm::vec3(0.8, 0.2, 0.2),  // albedo
    0.1f,                       // metallic
    0.8f                        // roughness
);

// Render
ral::beginFrame();
ral::renderMesh(mesh, glm::mat4(1.0f), material);
ral::endFrame();  // RAL handles queue sorting, batching, etc.
```

### Application API (Game-Level)

```cpp
class MyGame : public Application {
    void onInit() override {
        // Choose backend + pipeline
        useBackend(ghi::Backend::Metal);  // macOS
        usePipeline(ral::Pipeline::PBR);
        
        // Spawn geometry (uses RAL internally)
        spawnCube(glm::vec3(0, 0, 0), 10.0f, glm::vec3(1, 0, 0));
        loadModel("model.gltf");
        loadEnvironment("skybox.hdr");
    }
};
```

## Backend Implementation Status

| Backend | Status | Lines | Priority |
|---------|--------|-------|----------|
| **Vulkan** | Stub | ~200/2000 | High (Linux/Win) |
| **Metal** | Not Started | 0/2500 | **Highest** (macOS) |
| **OpenGL** | Not Started | 0/1500 | Medium (fallback) |
| **DX12** | Not Started | 0/2000 | Low (future) |

## Pipeline Implementation Status

| Pipeline | Status | Backends | Lines |
|----------|--------|----------|-------|
| **Simple** | Not Started | All | ~600 |
| **PBR** | Not Started | Vulkan, Metal | ~1200 |
| **Deferred** | Not Started | All | ~1000 |
| **Clustered** | Not Started | Vulkan, Metal | ~1500 |

## Completing the Implementation

### Step 1: Vulkan Backend (~8 hours)
Wrap existing `VulkanRenderer` into `GHI_VulkanBackend`:
- Resource pools (buffers, textures)
- Command recording
- Descriptor management
- Pipeline creation

### Step 2: RAL Core (~6 hours)
Implement `ral_core.cpp`:
- Mesh/material/light managers
- Pipeline selector
- Render queue sorting

### Step 3: SimplePipeline (~4 hours)
Create minimal forward renderer:
- `pipeline_simple.cpp`
- `simple.vert/frag` shaders
- Works on all backends

### Step 4: Metal Backend (~12 hours)
Native macOS rendering:
- `ghi_metal.mm`
- MTLDevice/MTLCommandQueue
- Argument buffers
- MSL shader variants

### Step 5: Application Refactor (~6 hours)
Simplify Application to use RAL:
- Remove PBR coupling
- Add `usePipeline()` API
- Reduce from 1673 → ~500 lines

**Total: ~36 hours** for full multi-backend system

## Current Recommendation

**Immediate priority:** Fix existing PBR rendering (landscape demo shows only blue)

**Then:** Implement GHI/RAL properly over multiple sessions

The architecture is now defined. Implementation is straightforward but time-intensive.

## References

- Plan: `/Users/bengamble/.cursor/plans/multi-backend_ghi_ral_renderer_587e526b.plan.md`
- Backend Strategy: `/docs/BACKEND_STRATEGY.md`
- GHI Philosophy: `/docs/GHI_DESIGN_PHILOSOPHY.md`
- Venus Reference: `/Users/bengamble/Venus/src/rendering/`
- HelloVulkan: `/vendored/hellovulkan/HelloVulkan/`
- vk-gltf-viewer: `/vendored/vk-gltf-viewer/`

