# Uniform Buffer Debugging

## Current Situation

**What works:** Geometry visible (flat magenta square with no transforms)  
**What doesn't:** Transforms (uniform buffers not reaching shader)

## Confirmed Facts

✅ Pipeline bound (shader id=1)  
✅ Vertex buffers bound (id=3, 7, 9)  
✅ Uniform buffers bound (camera=1 → index 0, object=5 → index 1)  
✅ Draw calls happen (36 indices per frame)  
✅ Camera matrices computed (view[0][0]=1.0, proj[0][0]=1.299)  
✅ Camera UBO updated (160 bytes)  

❌ **Uniform data NOT reaching shader**

## The Bug

Metal shaders declare:
```metal
constant CameraUniforms& camera [[buffer(0)]]
constant ObjectUniforms& object [[buffer(1)]]
```

We're binding with:
```cpp
encoder->setVertexBuffer(mtlBuffer, 0, bufferIndex);
```

**This might be the issue:** For small uniform data, Metal prefers `setVertexBytes()` not `setVertexBuffer()`.

## Solution to Try

Change `bindUniformBuffer()` to use `setVertexBytes()` instead:

```cpp
void GHI_MetalBackend::bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    // Get buffer data
    MTL::Buffer* mtlBuffer = ...;
    void* data = mtlBuffer->contents();
    size_t size = mtlBuffer->length();
    
    // Use setVertexBytes for uniforms (like SDL example)
    encoder->setVertexBytes(data, size, binding);
    encoder->setFragmentBytes(data, size, binding);
}
```

This copies the data directly instead of binding the buffer resource.

## Alternative: Check Buffer Contents

Maybe the buffer HAS data but it's wrong:

```cpp
MTL::Buffer* buf = static_cast<MTL::Buffer*>(buffers_[cameraUBO_.id]);
float* data = (float*)buf->contents();
printf("Camera buffer first 4 floats: %.3f %.3f %.3f %.3f\n", 
       data[0], data[1], data[2], data[3]);
```

Should print the first row of the view matrix.

## Next Step

Try `setVertexBytes()` approach - this is what the working SDL Metal example uses.
