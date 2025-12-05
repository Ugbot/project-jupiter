# GPU-Driven Rendering Implementation Plan

## Overview
This document tracks the implementation of GPU-driven rendering features for Project Jupiter.

## Completed Phases

### Phase 1: Bindless Infrastructure
- [x] Descriptor indexing support enabled in Vulkan context
- [x] Buffer device address support enabled
- [x] Modern synchronization (VK_KHR_synchronization2)
- [x] VMA allocator with buffer device address support

### Phase 1.5: ECS-Renderer Bridge (Arrow-Style)
- [x] `ecs_bridge.h/cpp` - Integration layer between ECS and rendering
- [x] `SimulationRunner` class moved to ECS module (headless support)
- [x] Decoupled architecture - game logic doesn't depend on renderer
- [x] Event sourcing patterns for state synchronization

### Phase 2: Indirect Draw & GPU Frustum Culling
- [x] `IndirectDrawBuffer` class (`indirect_draw_buffer.h/cpp`)
  - VkDrawIndirectCommand buffer management
  - AABB buffer for per-object bounds
  - Staging buffer pattern for CPU -> GPU uploads
  - Frustum extraction from view-projection matrix
- [x] `PipelineFrustumCulling` class (`pipeline_frustum_culling.h/cpp`)
  - Compute pipeline for GPU-side visibility testing
  - Descriptor management for frustum UBO, AABB SSBO, command SSBO
  - Memory barriers between compute and indirect draw stages
- [x] `frustum_culling.comp` compute shader
  - Per-object AABB vs frustum test
  - Modifies instanceCount to cull objects
  - Uses proper frustum-correct algorithm (iquilezles.org reference)
- [x] CMake integration for compute shader compilation

## Next Steps (Phase 3+)

### Phase 3: Multi-Draw Indirect
- [ ] Batch multiple objects into single draw call
- [ ] Per-mesh indirect draw command generation
- [ ] Draw count buffer for GPU-determined batch sizes
- [ ] `vkCmdDrawIndirectCount` support

### Phase 4: Instance Culling & LOD
- [ ] Per-instance frustum culling (for instanced objects)
- [ ] Distance-based LOD selection on GPU
- [ ] LOD transition (discrete or dithered)
- [ ] Instance ID mapping for material lookups

### Phase 5: Occlusion Culling (HZB)
- [ ] Hierarchical Z-Buffer generation
- [ ] GPU occlusion query via compute
- [ ] Two-pass culling (previous frame HZB + current frame)
- [ ] Conservative depth testing

### Phase 6: Scene Submission
- [ ] Scene graph traversal on CPU
- [ ] Object batching by material/pipeline
- [ ] Per-frame indirect buffer rebuild
- [ ] Integration with ECS transform components

## File Structure

```
rendering/
├── include/rendering/
│   ├── indirect_draw_buffer.h     # GPU indirect draw command management
│   └── pipeline_frustum_culling.h  # Compute pipeline for frustum culling
├── src/
│   ├── indirect_draw_buffer.cpp
│   └── pipeline_frustum_culling.cpp
└── shaders/
    └── compute/
        ├── CMakeLists.txt           # Compute shader compilation
        └── frustum_culling.comp     # GPU frustum culling shader
```

## Integration Points

### Using IndirectDrawBuffer
```cpp
IndirectDrawBuffer indirectBuffer;
indirectBuffer.initialize(device, allocator, maxObjects);

// Per-frame
indirectBuffer.beginBatch();
for (auto& object : visibleObjects) {
    GPUAABB aabb = computeAABB(object);
    indirectBuffer.addDrawCommandWithAABB(vertexCount, firstVertex, instanceIndex, aabb);
}
indirectBuffer.endBatch(cmd);
```

### Using PipelineFrustumCulling
```cpp
PipelineFrustumCulling cullingPipeline;
cullingPipeline.initialize(device, allocator, "shaders/compute/frustum_culling.comp.spv");

// Per-frame
cullingPipeline.updateFrustum(camera.viewProj, frameIndex);
cullingPipeline.bindIndirectBuffer(indirectBuffer, frameIndex);
cullingPipeline.execute(cmd, indirectBuffer, frameIndex);

// Then draw with indirect
vkCmdDrawIndirect(cmd, indirectBuffer.getCommandBuffer(), 0,
                  indirectBuffer.getDrawCount(), sizeof(VkDrawIndirectCommand));
```

## Performance Considerations

- Frustum culling runs at O(1) per object on GPU
- No CPU readback required - culling result stays on GPU
- Memory barrier ensures compute writes complete before indirect draw
- Workgroup size of 64 balances occupancy and divergence

## References

- [iquilezles.org/articles/frustumcorrect](https://iquilezles.org/articles/frustumcorrect/) - Proper frustum-AABB testing
- [Clustered Deferred and Forward Shading](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf) - Olsson et al.
- HelloVulkan (vendored) - Reference GPU-driven implementation
