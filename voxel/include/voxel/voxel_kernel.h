#pragma once

/**
 * @file voxel_kernel.h
 * @brief Voxel kernel definitions (Arrow-inspired compute kernels)
 *
 * Kernels are the execution units for voxel operations. Each kernel operates
 * on VoxelExecBatch input/output, enabling vectorized batch processing.
 */

#include "voxel_exec_batch.h"
#include <cstddef>

namespace jupiter {
namespace voxel {

// ============================================================================
// Status Codes
// ============================================================================

/**
 * @brief Status codes for voxel operations
 */
enum class VoxelStatus : int32_t {
    Ok = 0,                 ///< Success
    InvalidInput = -1,      ///< Invalid input data
    BufferFull = -2,        ///< Output buffer full
    NotFound = -3,          ///< Resource not found
    OutOfMemory = -4,       ///< Memory allocation failed
    InvalidState = -5,      ///< Operation not valid in current state
    Cancelled = -6,         ///< Operation was cancelled
    Unknown = -99,          ///< Unknown error
};

inline bool isOk(VoxelStatus status) {
    return status == VoxelStatus::Ok;
}

// ============================================================================
// Kernel Mode
// ============================================================================

/**
 * @brief Kernel execution mode
 */
enum class VoxelKernelMode : uint8_t {
    ReadOnly,       ///< Only reads input, safe for parallel execution
    ReadWrite,      ///< Modifies output, requires single-writer access
    Compute,        ///< Dispatches GPU compute work
};

// ============================================================================
// Kernel Context
// ============================================================================

// Forward declaration
class VoxelWorld;
class ChunkColumns;

/**
 * @brief Context passed to voxel kernels for runtime information
 */
struct VoxelKernelContext {
    float deltaTime = 0.0f;         ///< Frame delta time
    uint64_t frameNumber = 0;       ///< Current frame number
    uint64_t generation = 0;        ///< World generation (edit count)
    
    VoxelWorld* world = nullptr;    ///< Pointer to voxel world
    
    /// Neighbor chunk data (6 directions: +X, -X, +Y, -Y, +Z, -Z)
    const ChunkColumns* neighborChunks[6] = {};
    
    /// Vulkan context for compute kernels
    void* vulkanContext = nullptr;
    
    /// User-defined context
    void* userContext = nullptr;
    
    /// Random seed for procedural operations
    uint32_t seed = 0;
};

// ============================================================================
// Kernel Function Signatures
// ============================================================================

/**
 * @brief Kernel execution function signature
 *
 * @param input  Input batch containing source column data (read-only)
 * @param output Output batch for writing results
 * @param ctx    Kernel context with runtime information
 * @return Status indicating success or failure
 */
using VoxelKernelExec = VoxelStatus(*)(
    const VoxelExecBatch* input,
    VoxelExecBatch* output,
    const VoxelKernelContext* ctx
);

/**
 * @brief Optional kernel initialization function
 *
 * Called once before kernel execution to set up state.
 */
using VoxelKernelInit = VoxelStatus(*)(
    const VoxelExecBatch* input,
    void** state
);

/**
 * @brief Optional kernel cleanup function
 */
using VoxelKernelFinalize = void(*)(void* state);

// ============================================================================
// Voxel Kernel
// ============================================================================

/**
 * @brief Voxel kernel metadata and function pointers
 *
 * Describes a compute operation that can be registered with the
 * VoxelKernelRegistry and executed on chunk data.
 */
struct VoxelKernel {
    /// Kernel name (for lookup)
    const char* name = nullptr;
    
    /// Core execution function (required)
    VoxelKernelExec exec = nullptr;
    
    /// Optional lifecycle hooks
    VoxelKernelInit init = nullptr;
    VoxelKernelFinalize finalize = nullptr;
    
    /// Required input columns (bitmask)
    VoxelColumnId inputColumns = VoxelColumnId::None;
    
    /// Columns written by this kernel (bitmask)
    VoxelColumnId outputColumns = VoxelColumnId::None;
    
    /// Execution mode
    VoxelKernelMode mode = VoxelKernelMode::ReadWrite;
    
    /// Optimal batch size (1 = one chunk at a time)
    size_t preferredBatchSize = 1;
    
    /// Priority for execution ordering (lower = earlier)
    int32_t priority = 0;
    
    // ========================================================================
    // Validation
    // ========================================================================
    
    /**
     * @brief Check if this kernel is valid
     */
    bool isValid() const noexcept {
        return name != nullptr && exec != nullptr;
    }
    
    /**
     * @brief Check if input batch has all required columns
     */
    bool validateInput(const VoxelExecBatch& batch) const noexcept {
        return (static_cast<uint32_t>(batch.presentMask) &
                static_cast<uint32_t>(inputColumns)) ==
               static_cast<uint32_t>(inputColumns);
    }
    
    /**
     * @brief Check if this is a read-only kernel
     */
    bool isReadOnly() const noexcept {
        return mode == VoxelKernelMode::ReadOnly;
    }
    
    /**
     * @brief Check if this kernel runs on GPU
     */
    bool isCompute() const noexcept {
        return mode == VoxelKernelMode::Compute;
    }
};

// ============================================================================
// Kernel Builder
// ============================================================================

/**
 * @brief Builder for constructing VoxelKernel instances with fluent API
 */
class VoxelKernelBuilder {
public:
    explicit VoxelKernelBuilder(const char* name) noexcept {
        kernel_.name = name;
    }
    
    VoxelKernelBuilder& exec(VoxelKernelExec fn) noexcept {
        kernel_.exec = fn;
        return *this;
    }
    
    VoxelKernelBuilder& init(VoxelKernelInit fn) noexcept {
        kernel_.init = fn;
        return *this;
    }
    
    VoxelKernelBuilder& finalize(VoxelKernelFinalize fn) noexcept {
        kernel_.finalize = fn;
        return *this;
    }
    
    VoxelKernelBuilder& inputs(VoxelColumnId cols) noexcept {
        kernel_.inputColumns = cols;
        return *this;
    }
    
    VoxelKernelBuilder& outputs(VoxelColumnId cols) noexcept {
        kernel_.outputColumns = cols;
        return *this;
    }
    
    VoxelKernelBuilder& mode(VoxelKernelMode m) noexcept {
        kernel_.mode = m;
        return *this;
    }
    
    VoxelKernelBuilder& batchSize(size_t size) noexcept {
        kernel_.preferredBatchSize = size;
        return *this;
    }
    
    VoxelKernelBuilder& priority(int32_t p) noexcept {
        kernel_.priority = p;
        return *this;
    }
    
    /// Mark as read-only (convenience)
    VoxelKernelBuilder& readOnly() noexcept {
        kernel_.mode = VoxelKernelMode::ReadOnly;
        return *this;
    }
    
    /// Mark as compute (GPU)
    VoxelKernelBuilder& compute() noexcept {
        kernel_.mode = VoxelKernelMode::Compute;
        return *this;
    }
    
    VoxelKernel build() const noexcept {
        return kernel_;
    }
    
private:
    VoxelKernel kernel_;
};

} // namespace voxel
} // namespace jupiter



