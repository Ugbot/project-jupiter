#pragma once

/**
 * @file kernel_registry.h
 * @brief Central registry for kernel functions (Arrow-inspired function registry)
 * 
 * The KernelRegistry provides:
 * - Registration of named kernels at startup
 * - Lookup by name for dynamic dispatch
 * - Batch execution across World data
 */

#include "types.h"
#include "kernel.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace jupiter::ecs {

// Forward declarations
class World;
class ReadSnapshot;

/**
 * @brief Central registry for compute kernels
 * 
 * Singleton registry that stores all available kernels and provides
 * dispatch mechanisms for executing them on World data.
 * 
 * Thread Safety:
 * - Registration is thread-safe (uses mutex)
 * - Lookup is thread-safe after registration phase
 * - Execution thread safety depends on kernel mode
 */
class KernelRegistry {
public:
    /**
     * @brief Get the singleton instance
     */
    static KernelRegistry& instance();

    // Non-copyable
    KernelRegistry(const KernelRegistry&) = delete;
    KernelRegistry& operator=(const KernelRegistry&) = delete;

    // ========================================================================
    // Registration
    // ========================================================================

    /**
     * @brief Register a kernel with the given name
     * @param name Unique kernel name
     * @param kernel Kernel definition
     * @return true if registration succeeded, false if name already exists
     */
    bool registerKernel(const char* name, Kernel kernel);

    /**
     * @brief Register a kernel using a builder
     */
    bool registerKernel(const KernelBuilder& builder);

    /**
     * @brief Check if a kernel with the given name exists
     */
    [[nodiscard]] bool hasKernel(const char* name) const;

    /**
     * @brief Unregister a kernel by name
     * @return true if kernel was found and removed
     */
    bool unregisterKernel(const char* name);

    /**
     * @brief Get all registered kernel names
     */
    [[nodiscard]] std::vector<std::string> getKernelNames() const;

    /**
     * @brief Get count of registered kernels
     */
    [[nodiscard]] size_t kernelCount() const;

    // ========================================================================
    // Lookup
    // ========================================================================

    /**
     * @brief Find a kernel by name
     * @return Pointer to kernel if found, nullptr otherwise
     */
    [[nodiscard]] const Kernel* findKernel(const char* name) const;

    /**
     * @brief Get all kernels matching the given mode
     */
    [[nodiscard]] std::vector<const Kernel*> getKernelsByMode(KernelMode mode) const;

    /**
     * @brief Get all kernels sorted by priority
     */
    [[nodiscard]] std::vector<const Kernel*> getKernelsByPriority() const;

    // ========================================================================
    // Execution
    // ========================================================================

    /**
     * @brief Execute a kernel on a World (batched)
     * 
     * Splits the world data into batches and executes the kernel on each.
     * For ReadOnly kernels, this can be parallelized.
     * 
     * @param name Kernel name
     * @param world World to operate on
     * @param ctx Execution context (delta time, etc.)
     * @return Status of execution
     */
    Status execute(const char* name, World& world, const KernelContext& ctx);

    /**
     * @brief Execute a read-only kernel on a snapshot
     * 
     * Thread-safe execution on a stable read snapshot.
     * 
     * @param name Kernel name
     * @param snapshot Read snapshot to operate on
     * @param ctx Execution context
     * @return Status of execution
     */
    Status executeReadOnly(const char* name, const ReadSnapshot& snapshot, 
                           const KernelContext& ctx);

    /**
     * @brief Execute all kernels of a given mode in priority order
     */
    Status executeAll(KernelMode mode, World& world, const KernelContext& ctx);

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set default batch size for all kernels
     */
    void setDefaultBatchSize(size_t size);

    /**
     * @brief Get default batch size
     */
    [[nodiscard]] size_t defaultBatchSize() const { return defaultBatchSize_; }

private:
    KernelRegistry();
    ~KernelRegistry() = default;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Kernel> kernels_;
    size_t defaultBatchSize_ = DEFAULT_BATCH_SIZE;
};

// ============================================================================
// Registration Macros
// ============================================================================

/**
 * @brief Register a kernel at static initialization time
 * 
 * Usage:
 *   REGISTER_KERNEL(myKernel, myKernelExec, 
 *                   ColumnId::Positions | ColumnId::Velocities,
 *                   ColumnId::Positions);
 */
#define REGISTER_KERNEL(name, exec_fn, input_cols, output_cols)                 \
    static bool _kernel_reg_##name = []() {                                     \
        return ::jupiter::ecs::KernelRegistry::instance().registerKernel(       \
            #name,                                                              \
            ::jupiter::ecs::Kernel{                                             \
                #name, exec_fn, nullptr, nullptr,                               \
                input_cols, output_cols,                                        \
                ::jupiter::ecs::KernelMode::ReadWrite,                          \
                ::jupiter::ecs::DEFAULT_BATCH_SIZE, 0                           \
            });                                                                 \
    }()

/**
 * @brief Register a read-only kernel
 */
#define REGISTER_READ_KERNEL(name, exec_fn, input_cols, output_cols)            \
    static bool _kernel_reg_##name = []() {                                     \
        return ::jupiter::ecs::KernelRegistry::instance().registerKernel(       \
            #name,                                                              \
            ::jupiter::ecs::Kernel{                                             \
                #name, exec_fn, nullptr, nullptr,                               \
                input_cols, output_cols,                                        \
                ::jupiter::ecs::KernelMode::ReadOnly,                           \
                ::jupiter::ecs::DEFAULT_BATCH_SIZE, 0                           \
            });                                                                 \
    }()

/**
 * @brief Register a GPU compute kernel
 */
#define REGISTER_COMPUTE_KERNEL(name, exec_fn, input_cols, output_cols)         \
    static bool _kernel_reg_##name = []() {                                     \
        return ::jupiter::ecs::KernelRegistry::instance().registerKernel(       \
            #name,                                                              \
            ::jupiter::ecs::Kernel{                                             \
                #name, exec_fn, nullptr, nullptr,                               \
                input_cols, output_cols,                                        \
                ::jupiter::ecs::KernelMode::Compute,                            \
                ::jupiter::ecs::DEFAULT_BATCH_SIZE, 0                           \
            });                                                                 \
    }()

} // namespace jupiter::ecs

