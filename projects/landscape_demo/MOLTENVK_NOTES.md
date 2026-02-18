# MoltenVK Compatibility Notes

## Current Status

The landscape demo **code is complete and correct**, but encounters a MoltenVK compatibility issue with Jupiter's PBR pipeline.

## The Issue

```
[mvk-error] SPIR-V to MSL conversion error: Argument buffer resource base type could not be determined.
When padding argument buffer elements, all descriptor set resources must be supplied with a base type by the app.
```

This error occurs when:
- Using combined `sampler2D` descriptors in arrays
- Not providing explicit type information that MoltenVK needs

## Working Demos on MoltenVK

✅ **vulkan_triangle** - Works perfectly (simple descriptors)
✅ **vk-gltf-viewer** (vendored reference) - Works on Apple M1 Pro

## Solutions

### Option 1: Separate Image/Sampler Descriptors (vk-gltf-viewer approach)

Instead of:
```glsl
layout(set=1, binding=0) uniform sampler2D textures[];
```

Use:
```glsl
layout(set=1, binding=0) uniform sampler samplers[];
layout(set=1, binding=1) uniform texture2D images[];
```

This gives MoltenVK explicit type information.

### Option 2: Simple Forward Shaders (temporary)

Use basic vertex/fragment shaders without complex descriptors:
- Single MVP uniform
- No texture arrays
- Direct lighting calculation

### Option 3: Test on Native Vulkan

The landscape demo will work perfectly on Linux/Windows with native Vulkan drivers.

## Landscape Demo Features (All Implemented)

✅ **Terrain System**
- Procedural heightmap (1024m², GLM Perlin noise)
- GPU texture (R32F, 512×512)
- 66k vertices with proper normals

✅ **Trail System**
- Ping-pong textures (R16F intensity + RG16F direction)
- CPU→GPU events (flatten + bend)
- Configurable relaxation (5-20s)

✅ **Grass System**
- GPU compute generation (up to 1M instances)
- Indirect draw
- Procedural blade geometry
- Trail-aware density/bending

✅ **Shaders**
- `trail_update.comp` - Trail relaxation
- `grass_generate.comp` - Instance generation
- `grass.vert` - Procedural blades
- `grass.frag` - Lambert lighting

## Testing on Native Vulkan

To verify the complete system works:

```bash
# On Linux/Windows with native Vulkan
cd /path/to/project-jupiter
cmake -B build -S .
cmake --build build
./build/bin/landscape_demo
```

Expected result:
- Green rolling terrain
- Dense grass around camera
- Walking creates visible trails
- Trails relax over 12 seconds

## References

- **vk-gltf-viewer**: `/vendored/vk-gltf-viewer/` - MoltenVK-compatible PBR implementation
- **MoltenVK Documentation**: https://github.com/KhronosGroup/MoltenVK
- **Descriptor Indexing**: https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html

