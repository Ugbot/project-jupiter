# PBR/IBL MoltenVK Rebuild Plan

## Problem Analysis

**Current Status:**
- ✅ MoltenVK descriptor set creation works (no crash)
- ✅ Pipeline compiles successfully
- ✅ Auto-render executes every frame
- ❌ **Geometry not visible** (just blue sky/clear color)

**Root Cause:**
The PBR shaders were ported from HelloVulkan which uses desktop Vulkan features that don't translate well to MoltenVK's Metal backend. Even though the pipeline compiles, the actual rendering fails silently.

## Working Reference: vk-gltf-viewer

Confirmed working on Apple M1 Pro with MoltenVK. Key differences:

### 1. Separate Image/Sampler Descriptors
**Current (broken on MoltenVK):**
```glsl
layout(set=1, binding=0) uniform sampler2D albedoMap;
```

**vk-gltf-viewer (works):**
```glsl
layout(set=2, binding=3) uniform sampler samplers[];
layout(set=2, binding=4) uniform texture2D images[];
// Use: texture(sampler2D(images[idx], samplers[idx]), uv)
```

### 2. Runtime Descriptor Indexing
```glsl
#extension GL_EXT_nonuniform_qualifier : require
uint texIdx = material.baseColorTextureIndex;
vec4 color = texture(sampler2D(images[texIdx & 0xFFF], samplers[texIdx >> 12]), uv);
```

### 3. Simpler Descriptor Sets
- Set 0: Camera/view uniform
- Set 1: IBL resources (spherical harmonics, prefiltered, BRDF)
- Set 2: Materials buffer + texture arrays

### 4. Buffer Device Address for Vertex Data
Instead of binding vertex buffers, use BDA and pull vertices in shader:
```glsl
layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};
```

## Rebuild Strategy

### Phase 1: Simple Forward PBR (MoltenVK Compatible)
**Goal:** Get basic PBR working without advanced features

**Changes:**
1. **New shader pair**: `pbr_simple.vert` / `pbr_simple.frag`
   - Set 0: Camera UBO + single directional light
   - Set 1: Material properties UBO
   - Set 2: 5 individual texture bindings (albedo, normal, metallic, occlusion, emissive)
   - No IBL, no shadows, no SSAO initially
   - Push constants: model matrix only

2. **Simplified MaterialSystem**
   - Keep current descriptor layout (5 combined samplers work if not in arrays)
   - Add explicit flags to prevent argument buffer usage

3. **Test Path**
   - Triangle demo works → validates Vulkan basics
   - Simple PBR demo → validates material rendering
   - Add IBL → spherical harmonics + prefiltered map
   - Add shadows → separate pass
   - Full landscape → terrain + grass + trails

### Phase 2: Add IBL (vk-gltf-viewer approach)
- Compute shader for spherical harmonics (subgroup reductions)
- Prefiltered environment map generation
- BRDF LUT (already working in compute)
- Bind to Set 1 (separate from materials)

### Phase 3: Bindless Textures (Optional, for performance)
- Separate sampler/image arrays
- Material index → texture index mapping
- Nonuniform indexing

### Phase 4: Advanced Features
- Shadow mapping
- SSAO
- GPU-driven rendering integration

## Immediate Action

Create `pbr_simple.{vert,frag}` that:
- Uses only 2 descriptor sets (camera + material)
- No fancy features
- Should render geometry if MoltenVK Vulkan basics work

**Success criteria:**
- Primitives demo shows colored cubes/spheres
- Landscape demo shows green terrain
- No blue-only screen

## Files to Modify

1. `rendering/shaders/pbr/pbr_simple.vert` - NEW
2. `rendering/shaders/pbr/pbr_simple.frag` - NEW
3. `rendering/src/application.cpp` - Use simple shaders for now
4. Test, iterate, add features incrementally

## Timeline

- ✅ MoltenVK descriptor crash fixed
- 🔄 Simple PBR rendering (in progress)
- ⏳ IBL integration
- ⏳ Full landscape with grass/trails


