# Metal Backend Completion Plan

## Current Status

### What's Scaffolded ✅

**GHI Metal Backend Structure:**
- ✅ Header with all method signatures (`ghi_metal.h`)
- ✅ Implementation file with metal-cpp integration (`ghi_metal_impl.cpp`)
- ✅ Device/queue initialization (working)
- ✅ Buffer creation/destruction (working)
- ✅ Texture creation/destruction (working)
- ✅ Capability queries (complete)
- ✅ Memory management patterns (metal-cpp)

**Lines completed:** ~400 lines
**Lines remaining:** ~1500 lines

### What Needs Completion ⏳

**Critical for Rendering (Priority 1):**
1. **CAMetalLayer integration** (~200 lines)
   - Connect to window/view
   - Get drawable each frame
   - Present drawable

2. **Render Pass Descriptors** (~150 lines)
   - Create render pass with clear colors
   - Depth/stencil attachment
   - Load/store actions

3. **Render Pipeline State** (~300 lines)
   - Compile .metal shaders
   - Create pipeline descriptor
   - Vertex formats
   - Depth/blend/cull state

4. **Resource Binding** (~200 lines)
   - Vertex buffer binding (done, needs testing)
   - Index buffer binding
   - Uniform buffer binding (done, needs testing)
   - Texture binding (done, needs testing)

5. **Draw Command Execution** (~100 lines)
   - Draw primitives (done, needs index buffer)
   - Draw indexed (needs completion)
   - Set render pipeline state

**Advanced Features (Priority 2):**
6. **Compute Pipeline** (~150 lines)
7. **Indirect Draw** (~100 lines)
8. **Argument Buffers** (~200 lines)
9. **Tile Shaders** (~150 lines)
10. **Memoryless Textures** (~100 lines)

**Total Priority 1:** ~950 lines (critical path)
**Total Priority 2:** ~700 lines (advanced features)

---

## Phased Completion

### Phase 1: Basic Triangle (Minimal Metal Renderer)

**Goal:** Render a colored triangle using Metal backend

**Files to complete:**
```cpp
// ghi_metal_impl.cpp additions
1. CAMetalLayer setup
2. Render pass creation with clear color
3. Simple render pipeline (vertex + fragment)
4. Draw triangle (3 vertices)
```

**Test:** `./vulkan_triangle` but using Metal backend
**Success:** See colored triangle on screen
**Effort:** ~4 hours

### Phase 2: Textured Mesh Support

**Goal:** Render textured meshes with proper materials

**Additions:**
1. Texture sampling in fragment shader
2. Uniform buffer support (MVP matrices)
3. Index buffer binding
4. Multiple draw calls

**Test:** Simple cube with texture
**Success:** See rotating textured cube
**Effort:** ~3 hours

### Phase 3: Basic Lighting

**Goal:** Simple forward renderer with directional light

**Additions:**
1. Normal transformation
2. Light direction uniform
3. Lambertian shading
4. Multiple meshes in scene

**Test:** Multiple colored shapes with lighting
**Success:** primitives_demo via Metal
**Effort:** ~3 hours

### Phase 4: Terrain Rendering

**Goal:** Large mesh support (landscape demo)

**Additions:**
1. Large vertex buffer support
2. Frustum culling (optional)
3. LOD support (optional)

**Test:** landscape_demo terrain
**Success:** See green rolling hills
**Effort:** ~2 hours

---

## Minimal Metal Renderer (Phase 1 Detail)

### CAMetalLayer Setup

```cpp
// Get CAMetalLayer from window
void GHI_MetalBackend::setMetalLayer(void* layer) {
    metalLayer_ = layer;
    
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    CA::MetalLayer* caLayer = static_cast<CA::MetalLayer*>(layer);
    
    caLayer->setDevice(mtlDevice);
    caLayer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    caLayer->setFramebufferOnly(false);  // Allow reading
}
```

### Get Drawable (each frame)

```cpp
void GHI_MetalBackend::beginFrame() {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    MTL::CommandQueue* mtlQueue = static_cast<MTL::CommandQueue*>(commandQueue_);
    CA::MetalLayer* caLayer = static_cast<CA::MetalLayer*>(metalLayer_);
    
    // Get next drawable
    CA::MetalDrawable* drawable = caLayer->nextDrawable();
    if (!drawable) {
        pPool->release();
        return;  // Window minimized or not visible
    }
    
    currentDrawable_ = drawable;
    
    // Create command buffer
    MTL::CommandBuffer* cmdBuffer = mtlQueue->commandBuffer();
    currentCommandBuffer_ = cmdBuffer;
    
    pPool->release();
}
```

### Create Render Pass

```cpp
void GHI_MetalBackend::beginRenderPass() {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    MTL::CommandBuffer* cmdBuffer = static_cast<MTL::CommandBuffer*>(currentCommandBuffer_);
    CA::MetalDrawable* drawable = static_cast<CA::MetalDrawable*>(currentDrawable_);
    
    // Create render pass descriptor
    MTL::RenderPassDescriptor* passDesc = MTL::RenderPassDescriptor::renderPassDescriptor();
    
    // Color attachment (drawable texture)
    MTL::RenderPassColorAttachmentDescriptor* colorAttach = passDesc->colorAttachments()->object(0);
    colorAttach->setTexture(drawable->texture());
    colorAttach->setLoadAction(MTL::LoadActionClear);
    colorAttach->setStoreAction(MTL::StoreActionStore);
    
    // Clear color from current state
    MTL::ClearColor clearColor = MTL::ClearColor(
        currentState_.clearColor.r,
        currentState_.clearColor.g,
        currentState_.clearColor.b,
        currentState_.clearColor.a
    );
    colorAttach->setClearColor(clearColor);
    
    // Create render command encoder
    MTL::RenderCommandEncoder* encoder = cmdBuffer->renderCommandEncoder(passDesc);
    currentRenderEncoder_ = encoder;
    
    pPool->release();
}
```

### Compile Metal Shader

```cpp
ShaderHandle GHI_MetalBackend::createShader(const ShaderSource& source) {
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    MTL::Device* mtlDevice = static_cast<MTL::Device*>(device_);
    
    // Load .metal file or compile source
    NS::Error* error = nullptr;
    MTL::Library* library = nullptr;
    
    if (source.vertexPath) {
        // Load from file
        NS::String* path = NS::String::string(source.vertexPath, NS::UTF8StringEncoding);
        library = mtlDevice->newLibrary(path, &error);
    } else if (source.vertexSource) {
        // Compile from source
        NS::String* src = NS::String::string(source.vertexSource, NS::UTF8StringEncoding);
        MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
        library = mtlDevice->newLibrary(src, options, &error);
        options->release();
    }
    
    if (!library || error) {
        LOG_ERROR("GHI_Metal", "Failed to create shader library");
        pPool->release();
        return ShaderHandle{};
    }
    
    // Get vertex/fragment functions
    NS::String* vertName = NS::String::string("vertexMain", NS::UTF8StringEncoding);
    NS::String* fragName = NS::String::string("fragmentMain", NS::UTF8StringEncoding);
    
    MTL::Function* vertFunc = library->newFunction(vertName);
    MTL::Function* fragFunc = library->newFunction(fragName);
    
    if (!vertFunc || !fragFunc) {
        LOG_ERROR("GHI_Metal", "Failed to find vertex/fragment functions");
        library->release();
        pPool->release();
        return ShaderHandle{};
    }
    
    // Create render pipeline state
    MTL::RenderPipelineDescriptor* pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipelineDesc->setVertexFunction(vertFunc);
    pipelineDesc->setFragmentFunction(fragFunc);
    pipelineDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    
    MTL::RenderPipelineState* pipelineState = mtlDevice->newRenderPipelineState(pipelineDesc, &error);
    
    // Cleanup
    vertFunc->release();
    fragFunc->release();
    pipelineDesc->release();
    library->release();
    
    if (!pipelineState || error) {
        LOG_ERROR("GHI_Metal", "Failed to create render pipeline state");
        pPool->release();
        return ShaderHandle{};
    }
    
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    renderPipelines_[handle.id] = pipelineState;
    
    pPool->release();
    LOG_INFO("GHI_Metal", "Created shader pipeline: id=%u", handle.id);
    return handle;
}
```

### Simple Metal Shader (.metal file)

```metal
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    float3 position [[attribute(0)]];
    float3 color [[attribute(1)]];
};

struct Varyings {
    float4 position [[position]];
    float3 color;
};

vertex Varyings vertexMain(Vertex in [[stage_in]]) {
    Varyings out;
    out.position = float4(in.position, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 fragmentMain(Varyings in [[stage_in]]) {
    return float4(in.color, 1.0);
}
```

---

## Comparison: What Works Now vs What's Needed

### Current Implementation (Partial)

```
✅ Device creation (metal-cpp)
✅ Command queue creation
✅ Buffer creation/upload
✅ Texture creation
✅ Capability queries
⚠️ Command buffer (created but not used properly)
❌ CAMetalLayer (not connected)
❌ Render pass (stubbed)
❌ Pipeline state (stubbed)
❌ Actual drawing (stubbed)
```

### After Phase 1 Completion

```
✅ Device creation
✅ Command queue creation
✅ Buffer creation/upload
✅ Texture creation
✅ CAMetalLayer integration
✅ Render pass with clear
✅ Pipeline state from .metal file
✅ Draw triangle
✅ Present to screen
```

---

## Integration with Existing Code

### Window/View Setup

Jupiter needs SDL3 → CAMetalLayer bridge:

```cpp
// In window creation (platform layer)
#ifdef __APPLE__
    SDL_MetalView metalView = SDL_Metal_CreateView(sdlWindow);
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);
    
    // Pass to GHI
    ghi::setMetalLayer(layer);
#endif
```

### Render Loop

```cpp
// Triangle demo using Metal backend
ghi::initialize(ghi::Backend::Metal);

auto vbo = ghi::createBuffer({
    .type = ghi::BufferType::Vertex,
    .data = vertices,
    .size = sizeof(vertices)
});

auto shader = ghi::createShader({.vertexPath = "triangle.metal"});

while (!quit) {
    ghi::beginFrame();
    ghi::beginRenderPass();
    ghi::bindVertexBuffer(vbo);
    ghi::draw(3, 1);  // 3 vertices, 1 instance
    ghi::endRenderPass();
    ghi::endFrame();
}
```

---

## Effort Estimate

**Phase 1 (Basic Triangle):**
- Complete CAMetalLayer: 1 hour
- Complete render pass: 1 hour
- Complete shader loading: 2 hours
- Testing/debugging: 2 hours
- **Total: 6 hours**

**Phase 2 (Textured Mesh):**
- Texture binding: 1 hour
- Uniforms/MVP: 1 hour
- Index buffers: 1 hour
- **Total: 3 hours**

**Phase 3 (Forward Renderer):**
- SimplePipeline class: 2 hours
- Lighting shader: 1 hour
- Material system: 2 hours
- **Total: 5 hours**

**Phase 4 (Dual Backend):**
- Complete Vulkan GHI: 8 hours
- Test both backends: 2 hours
- **Total: 10 hours**

**Grand Total: ~24 hours** to working dual-backend forward renderer

---

## Recommendation

This is a **multi-session project**. For this session, I recommend:

**Option A: Document and Plan** (Done)
- ✅ Architecture scaffolded
- ✅ metal-cpp integrated
- ✅ APIs defined
- → Continue in future sessions

**Option B: Minimal Metal Triangle** (6 hours)
- Complete Phase 1
- See single triangle on Metal
- Proves metal-cpp works

**Option C: Fix Current PBR** (2 hours)
- Fix descriptor Set 0/1/2 mismatch
- Get current landscape working
- Do GHI/RAL refactor later

**My suggestion:** The foundation is solid. The full implementation is a proper multi-session architectural refactor that should be done carefully.


