/**
 * @file ghi_dx12.cpp
 * @brief GHI DirectX 12 Backend Implementation
 * 
 * Native Windows rendering using DirectX 12.
 */

#ifdef _WIN32

#include "ghi_dx12.h"
#include "logging/logging.h"

// Windows and D3D12 headers
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

// Link libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace jupiter {
namespace rendering {
namespace ghi {

// ============================================================================
// Constructor / Destructor
// ============================================================================

GHI_DX12Backend::GHI_DX12Backend() {
    LOG_INFO("GHI_DX12", "DirectX 12 backend created");
}

GHI_DX12Backend::~GHI_DX12Backend() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool GHI_DX12Backend::initialize() {
    LOG_WARN("GHI_DX12", "initialize() called without window handle - use initializeWithWindow()");
    return false;
}

void GHI_DX12Backend::waitIdle() {
    waitForGpu();
}

bool GHI_DX12Backend::initializeWithWindow(void* windowHandle) {
    if (initialized_) {
        LOG_WARN("GHI_DX12", "Already initialized");
        return true;
    }
    
    windowHandle_ = windowHandle;
    HWND hwnd = static_cast<HWND>(windowHandle);
    
    LOG_INFO("GHI_DX12", "Initializing DirectX 12 backend");
    
    // Get window size
    RECT rect;
    GetClientRect(hwnd, &rect);
    framebufferWidth_ = rect.right - rect.left;
    framebufferHeight_ = rect.bottom - rect.top;
    
    if (!createDevice()) {
        LOG_ERROR("GHI_DX12", "Failed to create D3D12 device");
        return false;
    }
    
    if (!createCommandQueue()) {
        LOG_ERROR("GHI_DX12", "Failed to create command queue");
        return false;
    }
    
    if (!createSwapchain()) {
        LOG_ERROR("GHI_DX12", "Failed to create swapchain");
        return false;
    }
    
    if (!createDescriptorHeaps()) {
        LOG_ERROR("GHI_DX12", "Failed to create descriptor heaps");
        return false;
    }
    
    if (!createRootSignature()) {
        LOG_ERROR("GHI_DX12", "Failed to create root signature");
        return false;
    }
    
    if (!createDepthBuffer()) {
        LOG_ERROR("GHI_DX12", "Failed to create depth buffer");
        return false;
    }
    
    // Create command allocators and command list
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        HRESULT hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocators_[i])
        );
        if (FAILED(hr)) {
            LOG_ERROR("GHI_DX12", "Failed to create command allocator %u", i);
            return false;
        }
    }
    
    HRESULT hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocators_[0],
        nullptr,
        IID_PPV_ARGS(&commandList_)
    );
    if (FAILED(hr)) {
        LOG_ERROR("GHI_DX12", "Failed to create command list");
        return false;
    }
    
    // Close command list initially
    commandList_->Close();
    
    // Create fence
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) {
        LOG_ERROR("GHI_DX12", "Failed to create fence");
        return false;
    }
    
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        LOG_ERROR("GHI_DX12", "Failed to create fence event");
        return false;
    }
    
    queryCapabilities();
    
    initialized_ = true;
    LOG_INFO("GHI_DX12", "DirectX 12 backend initialized successfully");
    return true;
}

void GHI_DX12Backend::shutdown() {
    if (!initialized_) return;
    
    LOG_INFO("GHI_DX12", "Shutting down DirectX 12 backend");
    
    waitForGpu();
    
    // Close fence event
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    
    // Release resources
    for (auto& [id, buffer] : buffers_) {
        if (buffer.resource) buffer.resource->Release();
    }
    buffers_.clear();
    
    for (auto& [id, texture] : textures_) {
        if (texture.resource) texture.resource->Release();
    }
    textures_.clear();
    
    for (auto& [id, shader] : shaders_) {
        if (shader.pso) shader.pso->Release();
    }
    shaders_.clear();
    
    // Release D3D12 objects
    if (depthBuffer_) { depthBuffer_->Release(); depthBuffer_ = nullptr; }
    if (dsvHeap_) { dsvHeap_->Release(); dsvHeap_ = nullptr; }
    
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        if (renderTargets_[i]) {
            renderTargets_[i]->Release();
            renderTargets_[i] = nullptr;
        }
    }
    
    if (rtvHeap_) { rtvHeap_->Release(); rtvHeap_ = nullptr; }
    if (srvHeap_) { srvHeap_->Release(); srvHeap_ = nullptr; }
    if (samplerHeap_) { samplerHeap_->Release(); samplerHeap_ = nullptr; }
    if (rootSignature_) { rootSignature_->Release(); rootSignature_ = nullptr; }
    
    if (commandList_) { commandList_->Release(); commandList_ = nullptr; }
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        if (commandAllocators_[i]) {
            commandAllocators_[i]->Release();
            commandAllocators_[i] = nullptr;
        }
    }
    
    if (fence_) { fence_->Release(); fence_ = nullptr; }
    if (swapchain_) { swapchain_->Release(); swapchain_ = nullptr; }
    if (commandQueue_) { commandQueue_->Release(); commandQueue_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
    
    initialized_ = false;
}

// ============================================================================
// Device Creation
// ============================================================================

bool GHI_DX12Backend::createDevice() {
#ifdef _DEBUG
    // Enable debug layer
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        LOG_INFO("GHI_DX12", "D3D12 debug layer enabled");
    }
#endif
    
    // Create DXGI factory
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        LOG_ERROR("GHI_DX12", "Failed to create DXGI factory");
        return false;
    }
    
    // Find high-performance adapter
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                         IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        
        // Try to create device
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
        if (SUCCEEDED(hr)) {
            char adapterName[128];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
            LOG_INFO("GHI_DX12", "Using adapter: %s", adapterName);
            break;
        }
    }
    
    if (!device_) {
        LOG_ERROR("GHI_DX12", "No suitable D3D12 adapter found");
        return false;
    }
    
    return true;
}

bool GHI_DX12Backend::createCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    
    HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
    return SUCCEEDED(hr);
}

bool GHI_DX12Backend::createSwapchain() {
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;
    
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.Width = framebufferWidth_;
    swapchainDesc.Height = framebufferHeight_;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.Stereo = FALSE;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.BufferCount = FRAME_COUNT;
    swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    
    ComPtr<IDXGISwapChain1> swapchain1;
    hr = factory->CreateSwapChainForHwnd(
        commandQueue_,
        static_cast<HWND>(windowHandle_),
        &swapchainDesc,
        nullptr,
        nullptr,
        &swapchain1
    );
    
    if (FAILED(hr)) {
        LOG_ERROR("GHI_DX12", "Failed to create swapchain");
        return false;
    }
    
    hr = swapchain1.As(&swapchain_);
    if (FAILED(hr)) return false;
    
    // Get render target views
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&renderTargets_[i]));
        if (FAILED(hr)) return false;
    }
    
    return true;
}

bool GHI_DX12Backend::createDescriptorHeaps() {
    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT + 8;  // Swapchain + custom RTs
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    
    HRESULT hr = device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
    if (FAILED(hr)) return false;
    
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    
    // Create RTVs for swapchain
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        device_->CreateRenderTargetView(renderTargets_[i], nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize_;
    }
    
    // DSV heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1 + 8;  // Main + custom RTs
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    
    hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    if (FAILED(hr)) return false;
    
    // SRV/CBV/UAV heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1024;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    hr = device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap_));
    if (FAILED(hr)) return false;
    
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    // Sampler heap
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 64;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    
    hr = device_->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&samplerHeap_));
    if (FAILED(hr)) return false;
    
    samplerDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    
    return true;
}

bool GHI_DX12Backend::createRootSignature() {
    // Create a flexible root signature that matches GHI binding model
    D3D12_ROOT_PARAMETER rootParams[8] = {};
    
    // Root param 0: CBV for camera (b0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Root param 1: CBV for lighting (b1)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 1;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Root param 2: CBV for material (b2)
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 2;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Root param 3: 32-bit constants for push constants
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[3].Constants.ShaderRegister = 3;
    rootParams[3].Constants.RegisterSpace = 0;
    rootParams[3].Constants.Num32BitValues = 64;  // 256 bytes
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Static samplers
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    
    // Linear sampler
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    // Point sampler
    staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].RegisterSpace = 0;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 4;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 2;
    rootSigDesc.pStaticSamplers = staticSamplers;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            LOG_ERROR("GHI_DX12", "Root signature error: %s", (char*)error->GetBufferPointer());
        }
        return false;
    }
    
    hr = device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                       signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    return SUCCEEDED(hr);
}

bool GHI_DX12Backend::createDepthBuffer() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = framebufferWidth_;
    depthDesc.Height = framebufferHeight_;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthBuffer_)
    );
    
    if (FAILED(hr)) return false;
    
    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    
    device_->CreateDepthStencilView(depthBuffer_, &dsvDesc,
                                    dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    
    return true;
}

void GHI_DX12Backend::waitForGpu() {
    if (!commandQueue_ || !fence_) return;
    
    const uint64_t fenceValue = fenceValues_[currentFrame_];
    commandQueue_->Signal(fence_, fenceValue);
    
    if (fence_->GetCompletedValue() < fenceValue) {
        fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void GHI_DX12Backend::moveToNextFrame() {
    const uint64_t currentFenceValue = fenceValues_[currentFrame_];
    commandQueue_->Signal(fence_, currentFenceValue);
    
    currentFrame_ = swapchain_->GetCurrentBackBufferIndex();
    
    if (fence_->GetCompletedValue() < fenceValues_[currentFrame_]) {
        fence_->SetEventOnCompletion(fenceValues_[currentFrame_], fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    
    fenceValues_[currentFrame_] = currentFenceValue + 1;
}

void GHI_DX12Backend::queryCapabilities() {
    capabilities_.backendType = Backend::DX12;
    capabilities_.maxTextureSize = 16384;
    capabilities_.maxTextureUnits = 32;
    capabilities_.maxUniformBufferBindings = 16;
    capabilities_.maxVertexAttributes = 32;
    capabilities_.maxColorAttachments = 8;
    capabilities_.hasComputeShaders = true;
    capabilities_.hasIndirectDraw = true;
    capabilities_.hasStorageBuffers = true;
    capabilities_.hasGeometryShaders = true;
    capabilities_.hasTessellation = true;
    
    // Query actual limits from device
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
        LOG_INFO("GHI_DX12", "Resource binding tier: %d", options.ResourceBindingTier);
    }
}

// ============================================================================
// Buffer Management
// ============================================================================

BufferHandle GHI_DX12Backend::createBuffer(const BufferCreateInfo& info) {
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = info.size;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = (info.usage == BufferUsage::Dynamic) ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
    
    DX12BufferData bufData;
    bufData.size = info.size;
    
    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        (info.usage == BufferUsage::Dynamic) ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&bufData.resource)
    );
    
    if (FAILED(hr)) {
        LOG_ERROR("GHI_DX12", "Failed to create buffer");
        return BufferHandle{};
    }
    
    // Upload initial data
    if (info.data && info.usage == BufferUsage::Dynamic) {
        void* mapped;
        D3D12_RANGE readRange = {0, 0};
        bufData.resource->Map(0, &readRange, &mapped);
        memcpy(mapped, info.data, info.size);
        bufData.resource->Unmap(0, nullptr);
    }
    
    BufferHandle handle;
    handle.id = nextBufferID_++;
    buffers_[handle.id] = bufData;
    
    LOG_INFO("GHI_DX12", "Created buffer: id=%u, size=%zu", handle.id, info.size);
    return handle;
}

void GHI_DX12Backend::destroyBuffer(BufferHandle handle) {
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end()) {
        if (it->second.resource) it->second.resource->Release();
        buffers_.erase(it);
    }
}

void GHI_DX12Backend::updateBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    
    void* mapped;
    D3D12_RANGE readRange = {0, 0};
    it->second.resource->Map(0, &readRange, &mapped);
    memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
    it->second.resource->Unmap(0, nullptr);
}

// ============================================================================
// Texture Management
// ============================================================================

TextureHandle GHI_DX12Backend::createTexture(const TextureCreateInfo& info) {
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = info.width;
    texDesc.Height = info.height;
    texDesc.DepthOrArraySize = (info.type == TextureType::TextureCube) ? 6 : 1;
    texDesc.MipLevels = info.mipLevels > 0 ? info.mipLevels : 1;
    texDesc.Format = static_cast<DXGI_FORMAT>(convertFormat(info.format));
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    
    if (hasUsage(info.usage, TextureUsage::RenderTarget)) {
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (hasUsage(info.usage, TextureUsage::DepthStencil)) {
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (hasUsage(info.usage, TextureUsage::Storage)) {
        texDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    
    DX12TextureData texData;
    texData.width = info.width;
    texData.height = info.height;
    texData.format = info.format;
    
    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texData.resource)
    );
    
    if (FAILED(hr)) {
        LOG_ERROR("GHI_DX12", "Failed to create texture");
        return TextureHandle{};
    }
    
    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = static_cast<DXGI_FORMAT>(convertFormat(info.format));
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
    
    texData.srvIndex = nextSrvIndex_++;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += texData.srvIndex * srvDescriptorSize_;
    device_->CreateShaderResourceView(texData.resource, &srvDesc, srvHandle);
    
    TextureHandle handle;
    handle.id = nextTextureID_++;
    textures_[handle.id] = texData;
    
    LOG_INFO("GHI_DX12", "Created texture: id=%u, size=%ux%u", handle.id, info.width, info.height);
    return handle;
}

void GHI_DX12Backend::destroyTexture(TextureHandle handle) {
    auto it = textures_.find(handle.id);
    if (it != textures_.end()) {
        if (it->second.resource) it->second.resource->Release();
        textures_.erase(it);
    }
}

void GHI_DX12Backend::updateTexture(TextureHandle handle, uint32_t level, uint32_t x, uint32_t y,
                                     uint32_t width, uint32_t height, const void* data) {
    // TODO: Implement texture upload via staging buffer
    LOG_WARN("GHI_DX12", "updateTexture not yet implemented");
}

// ============================================================================
// Shader Management
// ============================================================================

ShaderHandle GHI_DX12Backend::createShader(const ShaderSource& source) {
    // TODO: Load HLSL, compile, create PSO
    LOG_WARN("GHI_DX12", "createShader: HLSL compilation not yet implemented");
    
    ShaderHandle handle;
    handle.id = nextShaderID_++;
    shaders_[handle.id] = DX12ShaderData{};
    
    return handle;
}

void GHI_DX12Backend::destroyShader(ShaderHandle handle) {
    auto it = shaders_.find(handle.id);
    if (it != shaders_.end()) {
        if (it->second.pso) it->second.pso->Release();
        shaders_.erase(it);
    }
}

ShaderHandle GHI_DX12Backend::createComputeShader(const ShaderSource& source) {
    LOG_WARN("GHI_DX12", "createComputeShader not yet implemented");
    return ShaderHandle{};
}

// ============================================================================
// Sampler Management
// ============================================================================

SamplerHandle GHI_DX12Backend::createSampler(const SamplerCreateInfo& info) {
    // Create sampler descriptor
    // Note: D3D12 uses static samplers in root signature or descriptor tables
    
    SamplerHandle handle;
    handle.id = nextSamplerID_++;
    samplers_[handle.id] = DX12SamplerData{};
    
    LOG_INFO("GHI_DX12", "Created sampler: id=%u", handle.id);
    return handle;
}

void GHI_DX12Backend::destroySampler(SamplerHandle handle) {
    samplers_.erase(handle.id);
}

void GHI_DX12Backend::bindSampler(SamplerHandle sampler, uint32_t set, uint32_t binding) {
    // Samplers bound via descriptor tables
}

// ============================================================================
// Frame Rendering
// ============================================================================

void GHI_DX12Backend::beginFrame() {
    commandAllocators_[currentFrame_]->Reset();
    commandList_->Reset(commandAllocators_[currentFrame_], nullptr);
    
    // Set descriptor heaps
    ID3D12DescriptorHeap* heaps[] = { srvHeap_, samplerHeap_ };
    commandList_->SetDescriptorHeaps(2, heaps);
    
    commandList_->SetGraphicsRootSignature(rootSignature_);
}

void GHI_DX12Backend::endFrame() {
    // Execute command list
    commandList_->Close();
    ID3D12CommandList* lists[] = { commandList_ };
    commandQueue_->ExecuteCommandLists(1, lists);
    
    // Present
    swapchain_->Present(1, 0);
    
    moveToNextFrame();
}

void GHI_DX12Backend::beginRenderPass() {
    // Transition render target to render target state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTargets_[currentFrame_];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList_->ResourceBarrier(1, &barrier);
    
    // Get RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += currentFrame_ * rtvDescriptorSize_;
    
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    
    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    
    // Clear
    const float clearColor[] = { 
        currentState_.clearColor.r, currentState_.clearColor.g, 
        currentState_.clearColor.b, currentState_.clearColor.a 
    };
    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 
                                         currentState_.clearDepth, 0, 0, nullptr);
    
    // Set viewport and scissor
    D3D12_VIEWPORT viewport = { 0, 0, static_cast<float>(framebufferWidth_), 
                                static_cast<float>(framebufferHeight_), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(framebufferWidth_), 
                           static_cast<LONG>(framebufferHeight_) };
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);
    
    currentRenderTarget_ = RenderTargetHandle{};
}

void GHI_DX12Backend::beginRenderPass(RenderTargetHandle target) {
    if (!target.isValid()) {
        beginRenderPass();
        return;
    }
    
    // Custom render target handling
    LOG_WARN("GHI_DX12", "Custom render targets not yet implemented");
}

void GHI_DX12Backend::endRenderPass() {
    // Transition back to present state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTargets_[currentFrame_];
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList_->ResourceBarrier(1, &barrier);
}

// ============================================================================
// Render Targets
// ============================================================================

RenderTargetHandle GHI_DX12Backend::createRenderTarget(const RenderTargetCreateInfo& info) {
    LOG_WARN("GHI_DX12", "createRenderTarget not yet implemented");
    return RenderTargetHandle{};
}

void GHI_DX12Backend::destroyRenderTarget(RenderTargetHandle handle) {
    customRenderTargets_.erase(handle.id);
}

TextureHandle GHI_DX12Backend::getRenderTargetColorTexture(RenderTargetHandle target, uint32_t index) {
    return TextureHandle{};
}

TextureHandle GHI_DX12Backend::getRenderTargetDepthTexture(RenderTargetHandle target) {
    return TextureHandle{};
}

void GHI_DX12Backend::resizeRenderTarget(RenderTargetHandle target, uint32_t width, uint32_t height) {
    LOG_WARN("GHI_DX12", "resizeRenderTarget not yet implemented");
}

// ============================================================================
// Drawing
// ============================================================================

void GHI_DX12Backend::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    commandList_->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void GHI_DX12Backend::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                   int32_t vertexOffset, uint32_t firstInstance) {
    commandList_->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void GHI_DX12Backend::drawIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    auto it = buffers_.find(indirectBuffer.id);
    if (it == buffers_.end()) return;
    
    // Need command signature for indirect draw
    LOG_WARN("GHI_DX12", "drawIndirect requires command signature - not yet implemented");
}

void GHI_DX12Backend::drawIndexedIndirect(BufferHandle indirectBuffer, uint32_t drawCount, uint32_t stride) {
    LOG_WARN("GHI_DX12", "drawIndexedIndirect not yet implemented");
}

// ============================================================================
// Compute
// ============================================================================

void GHI_DX12Backend::bindComputeShader(ShaderHandle shader) {
    // Switch to compute pipeline
}

void GHI_DX12Backend::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    commandList_->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void GHI_DX12Backend::dispatchIndirect(BufferHandle indirectBuffer) {
    LOG_WARN("GHI_DX12", "dispatchIndirect not yet implemented");
}

// ============================================================================
// State Management
// ============================================================================

void GHI_DX12Backend::setRenderState(const RenderState& state) {
    currentState_ = state;
    
    if (state.shader.isValid()) {
        bindShader(state.shader);
    }
}

void GHI_DX12Backend::getRenderState(RenderState& state) {
    state = currentState_;
}

void GHI_DX12Backend::bindShader(ShaderHandle shader) {
    auto it = shaders_.find(shader.id);
    if (it != shaders_.end() && it->second.pso) {
        commandList_->SetPipelineState(it->second.pso);
        currentShader_ = shader;
    }
}

void GHI_DX12Backend::setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    D3D12_VIEWPORT viewport = { static_cast<float>(x), static_cast<float>(y), 
                                static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    commandList_->RSSetViewports(1, &viewport);
}

void GHI_DX12Backend::setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    D3D12_RECT scissor = { static_cast<LONG>(x), static_cast<LONG>(y), 
                           static_cast<LONG>(x + width), static_cast<LONG>(y + height) };
    commandList_->RSSetScissorRects(1, &scissor);
}

// ============================================================================
// Resource Binding
// ============================================================================

void GHI_DX12Backend::bindVertexBuffer(BufferHandle buffer, uint32_t binding, size_t offset) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = it->second.resource->GetGPUVirtualAddress() + offset;
    vbView.SizeInBytes = static_cast<UINT>(it->second.size - offset);
    vbView.StrideInBytes = 32;  // Vertex3D size
    
    commandList_->IASetVertexBuffers(binding, 1, &vbView);
}

void GHI_DX12Backend::bindIndexBuffer(BufferHandle buffer, size_t offset) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    D3D12_INDEX_BUFFER_VIEW ibView = {};
    ibView.BufferLocation = it->second.resource->GetGPUVirtualAddress() + offset;
    ibView.SizeInBytes = static_cast<UINT>(it->second.size - offset);
    ibView.Format = DXGI_FORMAT_R32_UINT;
    
    commandList_->IASetIndexBuffer(&ibView);
}

void GHI_DX12Backend::bindUniformBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    // Bind as CBV via root parameter
    if (binding < 3) {
        commandList_->SetGraphicsRootConstantBufferView(binding, 
                                                         it->second.resource->GetGPUVirtualAddress());
    }
}

void GHI_DX12Backend::bindStorageBuffer(BufferHandle buffer, uint32_t set, uint32_t binding) {
    // TODO: Create UAV for storage buffer
}

void GHI_DX12Backend::bindTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    auto it = textures_.find(texture.id);
    if (it == textures_.end()) return;
    
    // Bind via descriptor table
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    srvHandle.ptr += it->second.srvIndex * srvDescriptorSize_;
    
    // Would need descriptor table root parameter for textures
}

void GHI_DX12Backend::bindStorageTexture(TextureHandle texture, uint32_t set, uint32_t binding) {
    // TODO: Create UAV for storage texture
}

void GHI_DX12Backend::setPushConstants(const void* data, uint32_t size, uint32_t offset) {
    uint32_t numValues = (size + 3) / 4;
    commandList_->SetGraphicsRoot32BitConstants(3, numValues, data, offset / 4);
}

// ============================================================================
// Synchronization
// ============================================================================

void GHI_DX12Backend::memoryBarrier() {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = nullptr;  // All resources
    commandList_->ResourceBarrier(1, &barrier);
}

void GHI_DX12Backend::bufferBarrier(BufferHandle buffer) {
    auto it = buffers_.find(buffer.id);
    if (it == buffers_.end()) return;
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = it->second.resource;
    commandList_->ResourceBarrier(1, &barrier);
}

void GHI_DX12Backend::textureBarrier(TextureHandle texture) {
    auto it = textures_.find(texture.id);
    if (it == textures_.end()) return;
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = it->second.resource;
    commandList_->ResourceBarrier(1, &barrier);
}

// ============================================================================
// Capabilities
// ============================================================================

const Capabilities& GHI_DX12Backend::getCapabilities() const {
    return capabilities_;
}

// ============================================================================
// Debug
// ============================================================================

void GHI_DX12Backend::setDebugName(BufferHandle buffer, const char* name) {
    auto it = buffers_.find(buffer.id);
    if (it != buffers_.end() && it->second.resource) {
        wchar_t wname[256];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
        it->second.resource->SetName(wname);
    }
}

void GHI_DX12Backend::setDebugName(TextureHandle texture, const char* name) {
    auto it = textures_.find(texture.id);
    if (it != textures_.end() && it->second.resource) {
        wchar_t wname[256];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
        it->second.resource->SetName(wname);
    }
}

void GHI_DX12Backend::pushDebugGroup(const char* name) {
    // PIX markers would go here
}

void GHI_DX12Backend::popDebugGroup() {
    // PIX markers would go here
}

// ============================================================================
// Format Conversion
// ============================================================================

uint32_t GHI_DX12Backend::convertFormat(Format format) {
    switch (format) {
        case Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case Format::RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::Depth16: return DXGI_FORMAT_D16_UNORM;
        case Format::Depth24: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case Format::Depth32F: return DXGI_FORMAT_D32_FLOAT;
        case Format::Depth24_Stencil8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case Format::Depth32F_Stencil8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

uint32_t GHI_DX12Backend::convertBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return D3D12_BLEND_ZERO;
        case BlendFactor::One: return D3D12_BLEND_ONE;
        case BlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::DstColor: return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::OneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha: return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        default: return D3D12_BLEND_ONE;
    }
}

uint32_t GHI_DX12Backend::convertBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return D3D12_BLEND_OP_ADD;
        case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min: return D3D12_BLEND_OP_MIN;
        case BlendOp::Max: return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_ADD;
    }
}

uint32_t GHI_DX12Backend::convertCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
        case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
        case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareOp::LessOrEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_LESS;
    }
}

uint32_t GHI_DX12Backend::convertCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None: return D3D12_CULL_MODE_NONE;
        case CullMode::Front: return D3D12_CULL_MODE_FRONT;
        case CullMode::Back: return D3D12_CULL_MODE_BACK;
        default: return D3D12_CULL_MODE_BACK;
    }
}

} // namespace ghi
} // namespace rendering
} // namespace jupiter

#endif // _WIN32

