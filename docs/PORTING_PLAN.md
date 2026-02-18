# Vulkan Porting Plan: New GHI/RAL Model

**Date:** December 16, 2025  
**Goal:** Port existing Vulkan infrastructure to new GHI/RAL architecture

## Current State

### What Works ✅
- **Metal backend:** Fully standalone, production-ready (~1300 lines)
  - Device initialization
  - Buffer management (metal-cpp)
  - Texture creation/binding
  - Shader compilation (.metal source)
  - Indexed drawing
  - Proper reference counting
  
- **GHI/RAL architecture:** Complete and tested
  - Multi-backend abstraction
  - Primitives system (cube, sphere, plane)
  - Clean API (`rendering/ghi.h`)
  - GLM integrated

### What Needs Work ⚠️
- **Vulkan backend:** Currently a wrapper around old VulkanRenderer
  - Depends on Application class
  - Can't initialize standalone
  - Needs complete rewrite to match Metal backend structure

## Porting Strategy

### Phase 1: Make Vulkan Standalone (Like Metal)
**Goal:** Vulkan backend that initializes directly, no Application dependency

**Tasks:**
1. ✅ Create `ghi_vulkan_standalone.cpp` skeleton
2. ⏳ **Port instance creation** (from VulkanRenderer::init)
3. ⏳ **Port device selection** (pickPhysicalDevice)
4. ⏳ **Port logical device creation** (createDevice)
5. ⏳ **Port VMA allocator** (createAllocator)
6. ⏳ **Port command pool** (createCommandPool)
7. ⏳ **Port sync objects** (fences, semaphores)

**Reference code:** `rendering/src/vulkan_backend.cpp` lines 150-800

### Phase 2: Surface + Swapchain Integration
**Goal:** Connect to SDL window like Metal does with CAMetalLayer

**Tasks:**
1. Add `setSurface(SDL_Window* window)` method
2. Create Vulkan surface from SDL (SDL_Vulkan_CreateSurface)
3. Port swapchain creation (VulkanSwapchain class)
4. Port image view creation
5. Test clear color rendering

**Reference code:**
- `vulkan_backend.cpp` VulkanSwapchain class
- `dual_backend_demo/src/main.cpp` SDL window creation

### Phase 3: Buffer Management (VMA)
**Goal:** Full buffer CRUD matching Metal backend

**Already implemented in standalone:**
- ✅ `createBuffer()` - VMA-based with proper usage flags
- ✅ `destroyBuffer()` - Clean VMA destruction
- ✅ `updateBuffer()` - Map/unmap for updates

**Still need:**
- Staging buffers for device-local uploads
- Buffer barriers/synchronization
- Device address support (for bindless)

**Reference code:** `vulkan_backend.cpp` VulkanBuffer class

### Phase 4: Texture Management
**Goal:** Texture creation/binding matching Metal

**Tasks:**
1. Port image creation (VkImage + VkImageView)
2. Port image layout transitions
3. Port texture uploads (staging buffer)
4. Port sampler creation
5. Port texture binding to descriptors

**Reference code:**
- `vulkan_backend.cpp` image helper functions
- `texture.cpp` texture loading

### Phase 5: Shader Pipeline
**Goal:** SPIR-V loading + pipeline creation

**Tasks:**
1. Load .spv files (compiled SPIR-V)
2. Create shader modules
3. Create graphics pipeline
4. Create descriptor set layouts
5. Allocate descriptor sets
6. Bind pipelines + descriptors

**Reference code:**
- `vulkan_backend.cpp` VulkanPipeline class
- `material_system.cpp` descriptor set management

### Phase 6: Rendering Commands
**Goal:** Command buffer recording + execution

**Tasks:**
1. `beginFrame()` - Acquire swapchain image
2. `beginRenderPass()` - Create + begin render pass
3. `bindVertexBuffer()` - vkCmdBindVertexBuffers
4. `bindIndexBuffer()` - vkCmdBindIndexBuffer
5. `bindUniformBuffer()` - via descriptor sets
6. `draw()`/`drawIndexed()` - vkCmdDraw*
7. `endRenderPass()` - vkCmdEndRenderPass
8. `endFrame()` - Submit + present

**Reference code:**
- `vulkan_backend.cpp` rendering methods
- `application.cpp` render loop

### Phase 7: Test & Validate
**Goal:** Cube renders identically on Metal and Vulkan

**Tasks:**
1. Test triangle (like we did for Metal)
2. Test 3D cube with MVP
3. Test lighting
4. Test textures
5. Performance comparison
6. Memory leak testing

## Implementation Order

### Week 1 (Current Session)
- [x] Metal backend complete
- [x] Primitives system
- [x] Clean API structure
- [ ] Vulkan standalone skeleton (started)

### Week 2 (Next Session - 4-6 hours)
- [ ] Vulkan standalone complete
- [ ] Cube renders on both Metal + Vulkan
- [ ] Feature parity (buffers, textures, shaders)

### Week 3 (Future - 6-8 hours)
- [ ] Advanced features (compute, indirect draw)
- [ ] PBR pipeline on both backends
- [ ] Performance optimization

## Code Organization

### New Structure
```
rendering/src/ghi/backends/
├── ghi_metal_complete.cpp      ✅ Standalone Metal (~1300 lines)
├── ghi_vulkan.cpp               ✅ Wrapper (for old Application)
├── ghi_vulkan_standalone.cpp    ⏳ Standalone Vulkan (WIP)
└── ghi_opengl.cpp               📋 Future
```

### Decision: When to Use Each?
- **ghi_metal_complete.cpp** - Always use for Metal (standalone)
- **ghi_vulkan_standalone.cpp** - Use for new demos (dual_backend_demo, etc.)
- **ghi_vulkan.cpp** - Keep for old demos (lighting_demo, voxel_demo) until ported

## Metal → Vulkan Translation Guide

| Metal Concept | Vulkan Equivalent |
|--------------|-------------------|
| MTL::Device | VkDevice + VkPhysicalDevice |
| MTL::CommandQueue | VkQueue |
| MTL::CommandBuffer | VkCommandBuffer |
| MTL::RenderCommandEncoder | vkCmdBegin/EndRenderPass |
| MTL::Buffer | VkBuffer + VmaAllocation |
| MTL::Texture | VkImage + VkImageView |
| MTL::RenderPipelineState | VkPipeline |
| MTL::Drawable | VkSwapchainKHR image |
| Autorelease pool | Manual Vulkan object lifetime |
| metal-cpp types | Raw Vulkan handles |

## Key Differences

### Memory Management
**Metal:**
- Reference counted (retain/release)
- Autorelease pools for temp objects
- Automatic synchronization

**Vulkan:**
- Manual creation/destruction
- VMA for allocation
- Explicit barriers/synchronization

### Shader Compilation
**Metal:**
- Runtime: .metal source → MTLLibrary
- Compile at load time

**Vulkan:**
- Offline: .glsl → .spv (via glslangValidator)
- Load precompiled SPIR-V

### Resource Binding
**Metal:**
- Direct buffer/texture binding by index
- `setVertexBuffer(buffer, 0, index)`
- Simple and direct

**Vulkan:**
- Descriptor sets (more complex)
- Layout → Pool → Allocate → Update → Bind
- More setup, more efficient at draw time

## Next Session Checklist

### Immediate (1-2 hours)
- [ ] Finish Vulkan standalone initialization
- [ ] Add SDL surface creation
- [ ] Create swapchain
- [ ] Test clear color

### Short-term (2-4 hours)
- [ ] Port buffer creation (VMA) - already done
- [ ] Port shader loading (SPIR-V)
- [ ] Port rendering commands
- [ ] Test cube on Vulkan

### Medium-term (4-8 hours)
- [ ] Port texture system
- [ ] Port descriptor sets
- [ ] Full feature parity
- [ ] Update old demos to use GHI/RAL

## Success Criteria

1. **Functional Parity:** Same cube renders on Metal and Vulkan
2. **Performance Parity:** Similar frame times (<5% difference)
3. **API Parity:** Same demo code works on both backends
4. **No Regressions:** Old demos still work

## Notes

- Keep Metal backend as reference implementation
- Vulkan is more verbose but more explicit
- Use VMA for all allocations (like Metal uses its allocator)
- Test frequently - don't let issues accumulate

## Resources

- `rendering/src/vulkan_backend.cpp` - Source of truth for Vulkan patterns
- `rendering/src/ghi/backends/ghi_metal_complete.cpp` - Target structure
- VulkanSDK documentation
- Vulkan Tutorial (vulkan-tutorial.com)

