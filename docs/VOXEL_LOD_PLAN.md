# Voxel LOD System Implementation Plan

Based on Oryol's StbVoxelDemo VisTree implementation.

## Overview

Implement a screen-space error based LOD system using a sparse quad-tree (VisTree) that:
1. Dynamically splits/merges nodes based on camera distance
2. Uses placeholder meshes during LOD transitions (never shows empty space)
3. Generates coarser meshes for distant terrain (same 32x32 voxel grid, larger world coverage)

## Key Components

### 1. VisTree (Visibility Tree)
- Sparse quad-tree with 8 hierarchical levels
- Level 0 = most detailed (32 world units per chunk)
- Level N = 2^N times less detailed (covers 2^N times more world space)

### 2. Screen-Space Error Metric
From Oryol (based on http://tulrich.com/geekstuff/sig-notes.pdf):
```cpp
float ScreenSpaceError(const VisBounds& bounds, int lvl, int posX, int posY) {
    const float delta = float(1 << lvl);  // Geometric error doubles per level
    const float D = MinDist(posX, posY, bounds) + 1.0f;
    float rho = (delta / D) * K;  // K = displayWidth / (2 * tan(fov/2))
    return rho;
}
```
- If `rho <= tau` (threshold ~15 pixels), node is detailed enough
- Otherwise, split into 4 children

### 3. LOD Voxel Generation
Key insight: Same 32x32 voxel grid samples larger world areas at coarser LODs:
```cpp
const float voxelSizeX = (bounds.x1 - bounds.x0) / Config::ChunkSizeXY;
// LOD 0: voxelSize = 1.0 (1 voxel = 1 world unit)
// LOD 1: voxelSize = 2.0 (1 voxel = 2 world units)
// LOD 2: voxelSize = 4.0 (1 voxel = 4 world units)
```

### 4. Placeholder System
When mesh isn't ready:
1. Prefer children (finer LOD) as placeholder if available
2. Fall back to parent (coarser LOD) if children unavailable
3. Never render empty space

## Implementation Steps

### Phase 1: Core VisTree Structure
- [ ] Create `voxel/include/voxel/vis_tree.h`
- [ ] Create `voxel/include/voxel/vis_node.h`
- [ ] Create `voxel/include/voxel/vis_bounds.h`
- [ ] Implement node allocation/deallocation
- [ ] Implement Split() and Merge() operations

### Phase 2: Screen-Space Error Traversal
- [ ] Implement K constant calculation from display width and FOV
- [ ] Implement MinDist() - minimum distance from camera to bounds
- [ ] Implement ScreenSpaceError() calculation
- [ ] Implement Traverse() - recursive tree traversal with split/merge decisions

### Phase 3: LOD-Aware Voxel Generation
- [ ] Modify VoxelMesher to accept bounds (world coordinates) instead of ChunkCoord
- [ ] Implement scale-based terrain sampling
- [ ] Ensure noise sampling uses world coordinates for consistent terrain across LODs

### Phase 4: Placeholder System
- [ ] Track parent/child relationships for placeholder fallback
- [ ] Implement gatherDrawNode() with placeholder logic
- [ ] Add GeomGenJob queue for async mesh generation

### Phase 5: Integration
- [ ] Replace StreamingManager with VisTree-based system
- [ ] Update VoxelWorld to use VisTree
- [ ] Update PipelineVoxel to render from VisTree draw list

## Configuration Constants

```cpp
namespace voxel_lod {
    constexpr int ChunkSizeXY = 32;      // Voxel resolution per chunk
    constexpr int ChunkSizeZ = 32;
    constexpr int NumLevels = 8;         // LOD levels (0=most detailed)
    constexpr float ScreenSpaceThreshold = 15.0f;  // Pixels
    constexpr int MaxNumNodes = 4096;
    constexpr int MaxChunksPerFrame = 4;  // Mesh generation budget
}
```

## Data Structures

### VisNode
```cpp
struct VisNode {
    enum Flags { GeomPending = 1 };
    static constexpr int16_t InvalidGeom = -1;
    static constexpr int16_t EmptyGeom = -2;
    static constexpr int16_t InvalidChild = -1;
    static constexpr int NumGeoms = 3;    // Multiple buffers if mesh is large
    static constexpr int NumChilds = 4;   // Quad-tree

    uint16_t flags = 0;
    int16_t geoms[NumGeoms] = {InvalidGeom, InvalidGeom, InvalidGeom};
    int16_t childs[NumChilds] = {InvalidChild, InvalidChild, InvalidChild, InvalidChild};

    bool IsLeaf() const { return childs[0] == InvalidChild; }
    bool HasGeom() const { return geoms[0] != InvalidGeom; }
    bool HasEmptyGeom() const { return geoms[0] == EmptyGeom; }
    bool NeedsGeom() const { return geoms[0] == InvalidGeom && !(flags & GeomPending); }
    bool WaitsForGeom() const { return flags & GeomPending; }
};
```

### VisBounds
```cpp
struct VisBounds {
    int x0, x1;  // World X range
    int y0, y1;  // World Z range (Y is up in our system)
};
```

### GeomGenJob
```cpp
struct GeomGenJob {
    int16_t nodeIndex;
    int level;
    VisBounds bounds;
    glm::vec3 scale;
    glm::vec3 translate;
};
```

## Rendering Changes

The vertex shader needs scale/translate uniforms per chunk:
```glsl
layout(push_constant) uniform ChunkPushConstants {
    vec4 chunkOffset;  // xyz = translate, w = unused
    vec4 scale;        // xyz = scale (varies by LOD level)
};

void main() {
    vec3 localPos = unpackPosition(inAttrVertex) * scale.xyz;
    vec3 worldPos = chunkOffset.xyz + localPos;
    gl_Position = camera.viewProjection * vec4(worldPos, 1.0);
}
```

## Benefits

1. **Massive view distances** - Coarse LODs cover huge areas with few triangles
2. **Smooth transitions** - Placeholder system prevents popping
3. **Memory efficient** - Only detailed nodes near camera
4. **Automatic adaptation** - Screen-space error handles any resolution/FOV
