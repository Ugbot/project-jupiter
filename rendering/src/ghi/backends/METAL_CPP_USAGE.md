# Using metal-cpp in Jupiter

## Overview

Jupiter uses **metal-cpp** (C++ wrapper for Metal) instead of Objective-C for the Metal backend.

**Why metal-cpp:**
- ✅ Pure C++ (no Objective-C mixing)
- ✅ Zero overhead (inline wrappers)
- ✅ Direct Metal API access
- ✅ Same performance as Objective-C
- ✅ Better integration with C++ codebase

**Sources:**
- metal-cpp library: https://github.com/bkaradzic/metal-cpp
- Apple documentation: https://developer.apple.com/metal/cpp/

## Setup

### 1. Vendored Dependency

metal-cpp is vendored in `/vendored/metal-cpp/` as a git submodule.

```bash
git submodule add https://github.com/bkaradzic/metal-cpp.git vendored/metal-cpp
git submodule update --init vendored/metal-cpp
```

### 2. CMake Integration

In `rendering/CMakeLists.txt`:

```cmake
# Add metal-cpp include path
if(APPLE)
    target_include_directories(rendering PRIVATE
        ${CMAKE_SOURCE_DIR}/vendored/metal-cpp
    )
    
    # Link Metal framework
    target_link_libraries(rendering PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
    )
endif()
```

### 3. Implementation File

**One .cpp file** must define the metal-cpp implementation:

```cpp
// ghi_metal_impl.cpp (ONLY HERE)
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
```

**All other files** just include headers:

```cpp
// ghi_metal.h, other files
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
```

## API Examples

### Creating a Device

**Objective-C:**
```objc
id<MTLDevice> device = MTLCreateSystemDefaultDevice();
[device release];
```

**metal-cpp C++:**
```cpp
MTL::Device* device = MTL::CreateSystemDefaultDevice();
device->release();

// Or with smart pointer
NS::SharedPtr<MTL::Device> device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
// Auto-released when out of scope
```

### Creating a Buffer

**Objective-C:**
```objc
id<MTLBuffer> buffer = [device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
```

**metal-cpp C++:**
```cpp
MTL::Buffer* buffer = device->newBuffer(data, size, MTL::ResourceStorageModeShared);
```

### Creating a Texture

**Objective-C:**
```objc
MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
[desc setPixelFormat:MTLPixelFormatRGBA8Unorm];
[desc setWidth:512];
[desc setHeight:512];
id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
[desc release];
```

**metal-cpp C++:**
```cpp
MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
desc->setWidth(512);
desc->setHeight(512);
MTL::Texture* texture = device->newTexture(desc);
desc->release();
```

### Command Buffer and Encoding

**Objective-C:**
```objc
id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
[encoder endEncoding];
[commandBuffer commit];
```

**metal-cpp C++:**
```cpp
MTL::CommandBuffer* commandBuffer = commandQueue->commandBuffer();
MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(passDesc);
encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, 3);
encoder->endEncoding();
commandBuffer->commit();
```

## Memory Management

metal-cpp follows Cocoa/CoreFoundation reference counting:

### Ownership Rules

1. **You own** objects from methods starting with `alloc`, `new`, `copy`, `mutableCopy`, `Create`
2. **You must release** objects you own
3. **Don't release** objects you don't own

### Manual Management

```cpp
MTL::Buffer* buffer = device->newBuffer(size, options);  // retainCount = 1
// ... use buffer ...
buffer->release();  // retainCount = 0, deallocated
```

### Smart Pointers (Recommended)

```cpp
NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(device->newBuffer(size, options));
// Auto-released when buffer goes out of scope
```

## GHI Integration

### Type Casting

GHI uses opaque pointers (`MTLDevice*` etc.) to avoid exposing metal-cpp in public headers:

```cpp
// ghi_metal.h (header, no metal-cpp exposure)
class GHI_MetalBackend {
private:
    MTLDevice* device_;  // Opaque pointer
};

// ghi_metal_impl.cpp (implementation, has metal-cpp)
MTL::Device* mtlDevice = reinterpret_cast<MTL::Device*>(device_);
mtlDevice->newBuffer(...);
```

### Buffer Creation Pattern

```cpp
BufferHandle GHI_MetalBackend::createBuffer(const BufferCreateInfo& info) {
    MTL::Device* mtlDevice = reinterpret_cast<MTL::Device*>(device_);
    
    // Create buffer (metal-cpp C++ API)
    MTL::Buffer* mtlBuffer = mtlDevice->newBuffer(info.data, info.size, MTL::ResourceStorageModeShared);
    
    // Store in handle map
    BufferHandle handle;
    handle.id = nextBufferID_++;
    buffers_[handle.id] = reinterpret_cast<MTLBuffer*>(mtlBuffer);
    
    return handle;
}
```

## Advantages for Jupiter

1. **Pure C++** - No Objective-C++, easier to maintain
2. **Type safety** - C++ templates and constexpr
3. **Zero overhead** - Inlined calls, same as Objective-C
4. **Modern C++** - Works with C++17 features
5. **Cross-platform** - Same API for macOS/iOS
6. **Debugging** - C++ debugger works natively

## References

- metal-cpp repo: https://github.com/bkaradzic/metal-cpp
- Apple docs: https://developer.apple.com/metal/cpp/
- Memory management: https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/MemoryMgmt/Articles/mmRules.html

