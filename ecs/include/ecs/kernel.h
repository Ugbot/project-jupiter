#pragma once

/**
 * @file kernel.h
 * @brief Kernel function definitions (Arrow-inspired compute kernels)
 * 
 * Kernels are the execution units for ECS systems. Each kernel operates
 * on ExecBatch input/output, enabling vectorized batch processing.
 */

#include "types.h"
#include "exec_batch.h"
#include <cstddef>

namespace jupiter::ecs {

/**
 * @brief Kernel execution function signature
 * 
 * Like Arrow's ArrayKernelExec, this is the core function pointer type
 * for all compute kernels. Kernels receive input data and produce output
 * data through ExecBatch structures.
 * 
 * @param input  Input batch containing source column data (read-only)
 * @param output Output batch for writing results (may alias input for in-place ops)
 * @return Status indicating success or failure
 * 
 * Thread Safety:
 * - ReadOnly kernels: safe to call from multiple reader threads
 * - ReadWrite kernels: must be called only from writer thread
 * - Compute kernels: may dispatch GPU work, synchronization handled internally
 */
using KernelExec = Status(*)(const ExecBatch* input, ExecBatch* output);

/**
 * @brief Optional kernel initialization function
 * 
 * Called once before kernel execution to set up per-invocation state.
 * Can be used to allocate scratch buffers, validate inputs, etc.
 */
using KernelInit = Status(*)(const ExecBatch* input, void** state);

/**
 * @brief Optional kernel cleanup function
 * 
 * Called after kernel execution to clean up per-invocation state.
 */
using KernelFinalize = void(*)(void* state);

/**
 * @brief Kernel metadata and function pointers
 * 
 * A Kernel describes a compute operation that can be registered with
 * the KernelRegistry and executed on World data.
 */
struct Kernel {
    // Identification
    const char* name = nullptr;
    
    // Core execution function (required)
    KernelExec exec = nullptr;
    
    // Optional lifecycle hooks
    KernelInit init = nullptr;
    KernelFinalize finalize = nullptr;
    
    // Column requirements (bitmasks)
    ColumnId inputColumns = ColumnId::None;   // Required input columns
    ColumnId outputColumns = ColumnId::None;  // Columns written by this kernel
    
    // Execution characteristics
    KernelMode mode = KernelMode::ReadWrite;  // Thread safety mode
    size_t preferredBatchSize = DEFAULT_BATCH_SIZE;  // Optimal batch size
    
    // Priority for execution ordering (lower = earlier)
    int32_t priority = 0;

    // ========================================================================
    // Validation
    // ========================================================================

    /**
     * @brief Check if this kernel is valid (has required fields)
     */
    [[nodiscard]] bool isValid() const noexcept {
        return name != nullptr && exec != nullptr;
    }

    /**
     * @brief Check if input batch has all required columns
     */
    [[nodiscard]] bool validateInput(const ExecBatch& batch) const noexcept {
        return (static_cast<uint32_t>(batch.presentMask) & 
                static_cast<uint32_t>(inputColumns)) == 
               static_cast<uint32_t>(inputColumns);
    }

    /**
     * @brief Check if this is a read-only kernel
     */
    [[nodiscard]] bool isReadOnly() const noexcept {
        return mode == KernelMode::ReadOnly;
    }

    /**
     * @brief Check if this kernel runs on GPU
     */
    [[nodiscard]] bool isCompute() const noexcept {
        return mode == KernelMode::Compute;
    }
};

/**
 * @brief Builder for constructing Kernel instances with fluent API
 */
class KernelBuilder {
public:
    KernelBuilder(const char* name) noexcept {
        kernel_.name = name;
    }

    KernelBuilder& exec(KernelExec fn) noexcept {
        kernel_.exec = fn;
        return *this;
    }

    KernelBuilder& init(KernelInit fn) noexcept {
        kernel_.init = fn;
        return *this;
    }

    KernelBuilder& finalize(KernelFinalize fn) noexcept {
        kernel_.finalize = fn;
        return *this;
    }

    KernelBuilder& inputs(ColumnId cols) noexcept {
        kernel_.inputColumns = cols;
        return *this;
    }

    KernelBuilder& outputs(ColumnId cols) noexcept {
        kernel_.outputColumns = cols;
        return *this;
    }

    KernelBuilder& mode(KernelMode m) noexcept {
        kernel_.mode = m;
        return *this;
    }

    KernelBuilder& batchSize(size_t size) noexcept {
        kernel_.preferredBatchSize = size;
        return *this;
    }

    KernelBuilder& priority(int32_t p) noexcept {
        kernel_.priority = p;
        return *this;
    }

    // Mark as read-only (convenience)
    KernelBuilder& readOnly() noexcept {
        kernel_.mode = KernelMode::ReadOnly;
        return *this;
    }

    // Mark as compute (GPU)
    KernelBuilder& compute() noexcept {
        kernel_.mode = KernelMode::Compute;
        return *this;
    }

    [[nodiscard]] Kernel build() const noexcept {
        return kernel_;
    }

private:
    Kernel kernel_;
};

/**
 * @brief Context passed to kernels for additional runtime information
 */
struct KernelContext {
    float deltaTime = 0.0f;           // Frame delta time
    uint64_t frameNumber = 0;         // Current frame
    uint64_t generation = 0;          // World generation (swap count)
    void* vulkanContext = nullptr;    // For compute kernels: Vulkan handles
    void* userContext = nullptr;      // User-defined context
};

/**
 * @brief Extended kernel execution with context
 * 
 * Alternative kernel signature that receives additional runtime context.
 */
using KernelExecWithContext = Status(*)(
    const ExecBatch* input, 
    ExecBatch* output, 
    const KernelContext* ctx
);

} // namespace jupiter::ecs

