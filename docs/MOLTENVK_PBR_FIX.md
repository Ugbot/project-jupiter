# MoltenVK PBR Pipeline Fix

## Problem

Jupiter's PBR pipeline fails to create on MoltenVK (macOS) with error:
```
[mvk-error] SPIR-V to MSL conversion error: Argument buffer resource base type could not be determined.
When padding argument buffer elements, all descriptor set resources must be supplied with a base type by the app.
```

## Root Cause

When `VK_EXT_descriptor_indexing` features are enabled (which Jupiter does for bindless support), MoltenVK tries to use Metal Argument Buffers for descriptor sets. However, our PBR shader descriptors don't provide enough type information for MoltenVK to properly translate them to Metal.

## Working Reference

The vendored `vk-gltf-viewer` (confirmed working on Apple M1 Pro) uses:
- Separate `sampler` and `texture2D` arrays
- `GL_EXT_nonuniform_qualifier` for runtime indexing
- Explicit type information MoltenVK can understand

## Fix Strategy

### Phase 1: Immediate Workaround (DONE)
Created `vulkan_triangle` demo which works because it doesn't use complex descriptors.

### Phase 2: Proper Fix Options

**Option A: Disable Descriptor Indexing for Material Sets**

Add flag to MaterialSystem descriptor set layout:
```cpp
VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags = {};
// Don't use argument buffers for this set
layoutInfo.pNext = &bindingFlags;
```

**Option B: Separate Sampler/Image Descriptors** (vk-gltf-viewer approach)

Change from:
```glsl
layout(set=1, binding=0) uniform sampler2D albedoMap;
```

To:
```glsl
layout(set=1, binding=0) uniform sampler materialSampler;
layout(set=1, binding=1) uniform texture2D albedoTex;
layout(set=1, binding=2) uniform texture2D normalTex;
// ...use: texture(sampler2D(albedoTex, materialSampler), uv)
```

**Option C: Add Descriptor Buffer Device Address** (if supported)

Provide explicit type info that MoltenVK argument buffers need.

## Testing

The landscape demo is fully implemented but blocked by this MoltenVK issue.

To test on native Vulkan:
```bash
# Linux/Windows
cmake -B build -S .
cmake --build build
./build/bin/landscape_demo
# Should show: terrain + grass + trails working perfectly
```

## References

- MoltenVK Argument Buffers: https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- vk-gltf-viewer: `/vendored/vk-gltf-viewer/` (working MoltenVK implementation)
- Descriptor Indexing: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_descriptor_indexing.html
