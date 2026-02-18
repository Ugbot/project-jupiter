# HelloVulkan Integration: Advanced Rendering Techniques for Project Jupiter

This document provides concrete implementation guidance for integrating HelloVulkan's advanced rendering techniques into Project Jupiter's abstraction layer.

## Quick Start: Adding Clustered Forward Shading

### 1. Extend Pipeline Types

```cpp
// Add to rendering.h
enum class PipelineType {
    Forward,
    Deferred,
    ClusteredForward,  // NEW: HelloVulkan technique
    Raytracing
};

// Add to Pipeline class
struct ClusterForwardConfig {
    uint32_t clusterCountX = 16;
    uint32_t clusterCountY = 8;
    uint32_t clusterCountZ = 24;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

class Pipeline {
public:
    void setClusterForwardConfig(const ClusterForwardConfig& config);
    void setLightSources(const std::vector<Light>& lights);
};
```

### 2. Implement Cluster Data Structures

Based on HelloVulkan's `ResourcesClusterForward.h`:

```cpp
// Cluster data for GPU
struct ClusterData {
    glm::vec4 minBounds;
    glm::vec4 maxBounds;
    uint32_t lightIndices[128];  // Up to 128 lights per cluster
    uint32_t lightCount;
};

// Light data structure
struct GPULight {
    glm::vec4 position;     // .w = type (0=point, 1=spot, 2=directional)
    glm::vec4 direction;    // .w = range
    glm::vec4 color;        // .w = intensity
    glm::vec4 data;         // spot angle, etc.
};
```

### 3. Add Compute Pipelines

```cpp
class ClusterBuildingPipeline : public ComputePipeline {
public:
    void buildClusters(const Camera& camera, ClusterData* clusterBuffer);
};

class LightCullingPipeline : public ComputePipeline {
public:
    void cullLights(const std::vector<GPULight>& lights,
                   ClusterData* clusterBuffer);
};
```

## PBR Material System Integration

### Material Definition

```cpp
// Based on HelloVulkan's material system
struct PBRMaterial {
    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> metallicMap;
    std::shared_ptr<Texture> aoMap;
    std::shared_ptr<Texture> emissiveMap;

    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float emissiveFactor = 0.0f;

    // Bindless texture indices
    uint32_t albedoIndex = 0;
    uint32_t normalIndex = 0;
    uint32_t roughnessIndex = 0;
    uint32_t metallicIndex = 0;
};
```

### IBL Support

```cpp
class IBLSystem {
public:
    void loadEnvironmentMap(const std::string& hdrPath);
    void generateIrradianceMap();
    void generatePrefilteredEnvironmentMap();
    void generateBRDFLUT();

    std::shared_ptr<Texture> getIrradianceMap() const;
    std::shared_ptr<Texture> getPrefilteredMap() const;
    std::shared_ptr<Texture> getBRDFLUT() const;
};
```

## Bindless Rendering System

### Descriptor Management

Based on HelloVulkan's `VulkanDescriptorManager.h`:

```cpp
class BindlessDescriptorManager {
public:
    void initialize(VkDevice device, uint32_t maxTextures = 10000);
    void cleanup();

    // Texture management
    uint32_t addTexture(VkImageView imageView, VkSampler sampler);
    void removeTexture(uint32_t index);
    VkDescriptorSet getTextureDescriptorSet() const;

    // Buffer management (BDA)
    VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) const;
};

class Texture {
public:
    // Bindless support
    uint32_t getBindlessIndex() const { return bindlessIndex_; }
    void setBindlessIndex(uint32_t index) { bindlessIndex_ = index; }

private:
    uint32_t bindlessIndex_ = 0;
};
```

### Buffer Device Address (BDA)

```cpp
class Buffer {
public:
    VkDeviceAddress getDeviceAddress() const;
    void* mapForBDA() const;  // Map for device address access
};
```

## Compute-Based Frustum Culling

### Indirect Draw Integration

```cpp
// Based on HelloVulkan's indirect draw system
struct DrawCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    uint32_t vertexOffset;
    uint32_t firstInstance;
};

class IndirectDrawBuffer {
public:
    void initialize(VkDevice device, uint32_t maxCommands);
    void updateCommands(const std::vector<DrawCommand>& commands);
    VkBuffer getBuffer() const { return buffer_; }

private:
    VkBuffer buffer_;
    VmaAllocation allocation_;
};

class Mesh {
public:
    void enableIndirectDrawing();
    void setDrawCommandBuffer(IndirectDrawBuffer* buffer);
    void updateInstanceData(const std::vector<glm::mat4>& transforms);
};
```

### Culling Pipeline

```cpp
class FrustumCullingPipeline : public ComputePipeline {
public:
    void cullObjects(const std::vector<AABB>& objectBounds,
                    const Camera& camera,
                    IndirectDrawBuffer& drawBuffer);

private:
    struct CullingData {
        glm::mat4 viewProjectionMatrix;
        glm::vec4 frustumPlanes[6];
        uint32_t objectCount;
    };
};
```

## Compute-Based Skinning

### Skeletal Animation Data

```cpp
struct BoneData {
    glm::mat4 transform;
    glm::mat4 inverseBindPose;
};

struct AnimationFrame {
    std::vector<BoneData> bones;
    float timestamp;
};

class Skeleton {
public:
    void setBones(const std::vector<BoneData>& bones);
    void setAnimations(const std::vector<AnimationFrame>& frames);
    void updateAnimation(float time, float blendFactor = 0.0f);
    const std::vector<BoneData>& getCurrentPose() const;
};

class SkeletalMesh : public Mesh {
public:
    void setSkeleton(std::shared_ptr<Skeleton> skeleton);
    void updateSkinning(float deltaTime);

private:
    std::shared_ptr<Skeleton> skeleton_;
    VkBuffer boneBuffer_;  // SSBO for bone transforms
};
```

### Skinning Compute Pipeline

```cpp
class SkinningPipeline : public ComputePipeline {
public:
    void skinMesh(SkeletalMesh& mesh, const Skeleton& skeleton);

private:
    struct SkinningData {
        glm::mat4 boneTransforms[256];  // Max 256 bones
        uint32_t boneCount;
    };
};
```

## Advanced Shadow Mapping

### Cascade Shadow Maps

```cpp
struct CascadeInfo {
    glm::mat4 viewProjectionMatrix;
    float splitDepth;
    glm::vec2 texelSize;
};

class CascadeShadowMap {
public:
    void initialize(uint32_t cascadeCount = 4, uint32_t resolution = 2048);
    void updateCascades(const Camera& camera, const DirectionalLight& light);
    void render(const std::vector<Mesh*>& meshes);

    const std::vector<CascadeInfo>& getCascades() const;
    VkImageView getShadowMapView() const;

private:
    std::vector<CascadeInfo> cascades_;
    VkImage shadowMap_;
    VkImageView shadowMapView_;
    uint32_t cascadeCount_;
};
```

### Poisson Disk Sampling

```cpp
class ShadowSampling {
public:
    static glm::vec2 getPoissonSample(uint32_t index, uint32_t sampleCount = 16);

    // Precomputed Poisson disk samples
    static const std::vector<glm::vec2> poissonDisk16;
    static const std::vector<glm::vec2> poissonDisk25;
};
```

## Implementation Priority

### Phase 1: Core Infrastructure (Week 1-2)
1. ✅ VulkanContext integration
2. ✅ Pipeline base classes
3. ⏳ Bindless descriptor management
4. ⏳ Buffer device address support

### Phase 2: Rendering Techniques (Week 3-6)
1. ⏳ Clustered forward shading
2. ⏳ PBR material system
3. ⏳ Compute-based frustum culling
4. ⏳ Advanced shadow mapping

### Phase 3: Advanced Features (Week 7-10)
1. ⏳ Ray tracing support
2. ⏳ Compute skinning
3. ⏳ SSAO integration
4. ⏳ Post-processing pipeline

## Code Example: Complete Integration

```cpp
// Initialize advanced renderer
auto renderer = jupiter::rendering::Renderer::instance();

// Enable advanced features
renderer.enableClusteredShading(true);
renderer.enableBindlessRendering(true);
renderer.enableRayTracing(true);

// Set up PBR pipeline
auto pbrPipeline = renderer.createPipeline();
pbrPipeline->setType(Pipeline::Type::ClusteredForward);

// Configure clustered rendering
ClusterForwardConfig clusterConfig;
clusterConfig.clusterCountX = 16;
clusterConfig.clusterCountY = 8;
clusterConfig.clusterCountZ = 24;
pbrPipeline->setClusterForwardConfig(clusterConfig);

// Load PBR material
auto material = std::make_shared<PBRMaterial>();
material->albedoMap = renderer.createTexture()->loadFromFile("albedo.png");
material->normalMap = renderer.createTexture()->loadFromFile("normal.png");
material->metallicMap = renderer.createTexture()->loadFromFile("metallic.png");
material->roughnessMap = renderer.createTexture()->loadFromFile("roughness.png");

// Set up IBL
auto iblSystem = renderer.createIBLSystem();
iblSystem->loadEnvironmentMap("environment.hdr");

// Create scene with many lights
std::vector<Light> lights;
for (int i = 0; i < 1000; ++i) {
    lights.push_back(createRandomLight());
}

// Render loop
renderer.beginFrame();
renderer.setPipeline(pbrPipeline.get());

// Draw with advanced lighting
for (auto& object : sceneObjects) {
    renderer.drawMesh(object.mesh, object.material, object.transform);
}

renderer.endFrame();
```

## Performance Targets

Based on HelloVulkan benchmarks:
- **Clustered Forward**: 1000+ dynamic lights at 60-100 FPS
- **Frustum Culling**: < 0.025ms for 10,000 AABB tests
- **Compute Skinning**: GPU-only animation updates
- **Bindless Rendering**: Zero texture binding overhead

## Runtime Render Settings (Jupiter)
- `Application::getRenderSettingsMutable()` exposes toggles for IBL, shadows, SSAO, exposure, light falloff, albedo boost, and optional reflection LOD clamp. Changes take effect next frame without pipeline rebuilds.
- `setIBLEnabled` / `setShadowsEnabled` / `setSSAOEnabled` provide convenience helpers for game settings menus.
- PBR push constants now derive from these settings so the shader respects runtime changes (e.g., disabling IBL sets `FLAG_DISABLE_IBL`).

## Tracy Profiling Quickstart
- Tracy is enabled via `JUPITER_ENABLE_TRACY` (default ON) and instrumented in the render loop and IBL generation (`profiling/profiler.h` macros).
- Capture GPU/CPU timings by running the Tracy UI and connecting to the running app; zones are labeled with the function names (e.g., `PipelineIBL::filterSpecularPrefiltered`, `Application::renderPBRScene`).

This integration plan transforms Project Jupiter from a basic Vulkan wrapper into a high-performance rendering engine capable of competing with commercial game engines.
