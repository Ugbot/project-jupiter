#pragma once

/**
 * @file voxel_kernel_registry.h
 * @brief Registry for voxel kernels
 *
 * Central registry for registering and executing voxel kernels.
 * Mirrors the ECS KernelRegistry pattern.
 */

#include "voxel_kernel.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

namespace jupiter {
namespace voxel {

/**
 * @brief Singleton registry for voxel kernels
 *
 * All kernels are registered here and can be looked up by name.
 * The registry supports executing kernels with automatic validation.
 */
class VoxelKernelRegistry {
public:
    /// Maximum number of registered kernels
    static constexpr size_t MAX_KERNELS = 64;
    
    /**
     * @brief Get the singleton instance
     */
    static VoxelKernelRegistry& instance() {
        static VoxelKernelRegistry registry;
        return registry;
    }
    
    // ========================================================================
    // Registration
    // ========================================================================
    
    /**
     * @brief Register a kernel
     *
     * @param kernel Kernel to register
     * @return true if registered successfully
     */
    bool registerKernel(const VoxelKernel& kernel) {
        if (!kernel.isValid()) {
            return false;
        }
        
        std::string name(kernel.name);
        if (kernelMap_.find(name) != kernelMap_.end()) {
            // Already registered
            return false;
        }
        
        if (kernels_.size() >= MAX_KERNELS) {
            return false;
        }
        
        size_t index = kernels_.size();
        kernels_.push_back(kernel);
        kernelMap_[name] = index;
        
        return true;
    }
    
    /**
     * @brief Register a kernel using builder pattern
     */
    bool registerKernel(VoxelKernelBuilder&& builder) {
        return registerKernel(builder.build());
    }
    
    /**
     * @brief Unregister a kernel by name
     */
    bool unregisterKernel(const char* name) {
        auto it = kernelMap_.find(name);
        if (it == kernelMap_.end()) {
            return false;
        }
        
        // Don't actually remove from vector (preserves indices)
        // Just clear the kernel
        kernels_[it->second] = VoxelKernel{};
        kernelMap_.erase(it);
        return true;
    }
    
    // ========================================================================
    // Lookup
    // ========================================================================
    
    /**
     * @brief Find a kernel by name
     *
     * @param name Kernel name
     * @return Pointer to kernel, or nullptr if not found
     */
    const VoxelKernel* find(const char* name) const {
        auto it = kernelMap_.find(name);
        if (it == kernelMap_.end()) {
            return nullptr;
        }
        return &kernels_[it->second];
    }
    
    /**
     * @brief Check if a kernel is registered
     */
    bool hasKernel(const char* name) const {
        return kernelMap_.find(name) != kernelMap_.end();
    }
    
    /**
     * @brief Get all registered kernel names
     */
    std::vector<const char*> kernelNames() const {
        std::vector<const char*> names;
        names.reserve(kernelMap_.size());
        
        for (const auto& [name, idx] : kernelMap_) {
            if (kernels_[idx].isValid()) {
                names.push_back(kernels_[idx].name);
            }
        }
        
        return names;
    }
    
    // ========================================================================
    // Execution
    // ========================================================================
    
    /**
     * @brief Execute a kernel by name
     *
     * @param name Kernel name
     * @param input Input batch
     * @param output Output batch
     * @param ctx Kernel context
     * @return Status from kernel execution
     */
    VoxelStatus execute(const char* name,
                        const VoxelExecBatch& input,
                        VoxelExecBatch& output,
                        const VoxelKernelContext& ctx) const {
        const VoxelKernel* kernel = find(name);
        if (!kernel) {
            return VoxelStatus::NotFound;
        }
        
        return execute(*kernel, input, output, ctx);
    }
    
    /**
     * @brief Execute a kernel directly
     *
     * @param kernel Kernel to execute
     * @param input Input batch
     * @param output Output batch
     * @param ctx Kernel context
     * @return Status from kernel execution
     */
    VoxelStatus execute(const VoxelKernel& kernel,
                        const VoxelExecBatch& input,
                        VoxelExecBatch& output,
                        const VoxelKernelContext& ctx) const {
        if (!kernel.isValid()) {
            return VoxelStatus::InvalidInput;
        }
        
        // Validate input columns
        if (!kernel.validateInput(input)) {
            return VoxelStatus::InvalidInput;
        }
        
        // Initialize kernel state if needed
        void* state = nullptr;
        if (kernel.init) {
            VoxelStatus initStatus = kernel.init(&input, &state);
            if (!isOk(initStatus)) {
                return initStatus;
            }
        }
        
        // Execute kernel
        VoxelStatus result = kernel.exec(&input, &output, &ctx);
        
        // Cleanup kernel state
        if (kernel.finalize && state) {
            kernel.finalize(state);
        }
        
        return result;
    }
    
    /**
     * @brief Execute a kernel in-place (same batch for input/output)
     */
    VoxelStatus executeInPlace(const char* name,
                               VoxelExecBatch& batch,
                               const VoxelKernelContext& ctx) const {
        return execute(name, batch, batch, ctx);
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * @brief Get number of registered kernels
     */
    size_t kernelCount() const {
        return kernelMap_.size();
    }
    
    /**
     * @brief Clear all registered kernels
     */
    void clear() {
        kernels_.clear();
        kernelMap_.clear();
    }
    
private:
    VoxelKernelRegistry() = default;
    ~VoxelKernelRegistry() = default;
    
    // Non-copyable
    VoxelKernelRegistry(const VoxelKernelRegistry&) = delete;
    VoxelKernelRegistry& operator=(const VoxelKernelRegistry&) = delete;
    
    /// Registered kernels (indexed by registration order)
    std::vector<VoxelKernel> kernels_;
    
    /// Name to index mapping
    std::unordered_map<std::string, size_t> kernelMap_;
};

// ============================================================================
// Convenience Macros
// ============================================================================

/**
 * @brief Register a kernel with the global registry
 */
#define VOXEL_REGISTER_KERNEL(kernel) \
    ::jupiter::voxel::VoxelKernelRegistry::instance().registerKernel(kernel)

/**
 * @brief Execute a kernel by name
 */
#define VOXEL_EXECUTE_KERNEL(name, input, output, ctx) \
    ::jupiter::voxel::VoxelKernelRegistry::instance().execute(name, input, output, ctx)

} // namespace voxel
} // namespace jupiter



