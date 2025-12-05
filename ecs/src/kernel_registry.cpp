/**
 * @file kernel_registry.cpp
 * @brief Implementation of the kernel registry
 */

#include "ecs/kernel_registry.h"
#include "ecs/world.h"
#include "ecs/read_snapshot.h"
#include <algorithm>
#include <cassert>

namespace jupiter::ecs {

// ============================================================================
// Singleton
// ============================================================================

KernelRegistry& KernelRegistry::instance() {
    static KernelRegistry instance;
    return instance;
}

KernelRegistry::KernelRegistry() = default;

// ============================================================================
// Registration
// ============================================================================

bool KernelRegistry::registerKernel(const char* name, Kernel kernel) {
    if (!name || !kernel.exec) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    auto [it, inserted] = kernels_.try_emplace(name, kernel);
    return inserted;
}

bool KernelRegistry::registerKernel(const KernelBuilder& builder) {
    Kernel kernel = builder.build();
    return registerKernel(kernel.name, kernel);
}

bool KernelRegistry::hasKernel(const char* name) const {
    if (!name) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    return kernels_.find(name) != kernels_.end();
}

bool KernelRegistry::unregisterKernel(const char* name) {
    if (!name) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    return kernels_.erase(name) > 0;
}

std::vector<std::string> KernelRegistry::getKernelNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> names;
    names.reserve(kernels_.size());
    for (const auto& [name, kernel] : kernels_) {
        names.push_back(name);
    }
    return names;
}

size_t KernelRegistry::kernelCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return kernels_.size();
}

// ============================================================================
// Lookup
// ============================================================================

const Kernel* KernelRegistry::findKernel(const char* name) const {
    if (!name) return nullptr;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = kernels_.find(name);
    if (it != kernels_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const Kernel*> KernelRegistry::getKernelsByMode(KernelMode mode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<const Kernel*> result;
    for (const auto& [name, kernel] : kernels_) {
        if (kernel.mode == mode) {
            result.push_back(&kernel);
        }
    }
    return result;
}

std::vector<const Kernel*> KernelRegistry::getKernelsByPriority() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<const Kernel*> result;
    result.reserve(kernels_.size());
    for (const auto& [name, kernel] : kernels_) {
        result.push_back(&kernel);
    }
    
    std::sort(result.begin(), result.end(),
              [](const Kernel* a, const Kernel* b) {
                  return a->priority < b->priority;
              });
    
    return result;
}

// ============================================================================
// Execution
// ============================================================================

Status KernelRegistry::execute(const char* name, World& world, const KernelContext& ctx) {
    const Kernel* kernel = findKernel(name);
    if (!kernel) {
        return Status::NotFound;
    }

    if (!kernel->isValid()) {
        return Status::InvalidArgument;
    }

    // Get entity count from world
    const size_t entityCount = world.entityCount();
    if (entityCount == 0) {
        return Status::Ok;  // Nothing to process
    }

    // Determine batch size
    const size_t batchSize = kernel->preferredBatchSize > 0 
                           ? kernel->preferredBatchSize 
                           : defaultBatchSize_;

    // Initialize kernel state if needed
    void* state = nullptr;
    if (kernel->init) {
        // Create a batch for initialization
        ExecBatch initBatch = world.makeBatch(0, std::min(batchSize, entityCount));
        Status initStatus = kernel->init(&initBatch, &state);
        if (!isOk(initStatus)) {
            return initStatus;
        }
    }

    // Process in batches
    Status result = Status::Ok;
    for (size_t offset = 0; offset < entityCount && isOk(result); offset += batchSize) {
        const size_t currentBatchSize = std::min(batchSize, entityCount - offset);
        
        // Create input and output batches
        ExecBatch inputBatch = world.makeReadBatch(offset, currentBatchSize);
        ExecBatch outputBatch = world.makeWriteBatch(offset, currentBatchSize);
        
        // Set user data from context
        inputBatch.userData = const_cast<void*>(static_cast<const void*>(&ctx));
        outputBatch.userData = inputBatch.userData;
        inputBatch.batchIndex = offset / batchSize;
        outputBatch.batchIndex = inputBatch.batchIndex;
        
        // Execute kernel on this batch
        result = kernel->exec(&inputBatch, &outputBatch);
    }

    // Finalize kernel state
    if (kernel->finalize && state) {
        kernel->finalize(state);
    }

    // Mark output columns as dirty
    if (isOk(result)) {
        world.markDirty(kernel->outputColumns);
    }

    return result;
}

Status KernelRegistry::executeReadOnly(const char* name, const ReadSnapshot& snapshot,
                                       const KernelContext& ctx) {
    const Kernel* kernel = findKernel(name);
    if (!kernel) {
        return Status::NotFound;
    }

    if (!kernel->isValid()) {
        return Status::InvalidArgument;
    }

    if (!kernel->isReadOnly()) {
        return Status::InvalidArgument;  // Must be read-only kernel
    }

    const size_t entityCount = snapshot.entityCount();
    if (entityCount == 0) {
        return Status::Ok;
    }

    const size_t batchSize = kernel->preferredBatchSize > 0 
                           ? kernel->preferredBatchSize 
                           : defaultBatchSize_;

    // Process in batches (read-only, so no output batch modification)
    Status result = Status::Ok;
    for (size_t offset = 0; offset < entityCount && isOk(result); offset += batchSize) {
        const size_t currentBatchSize = std::min(batchSize, entityCount - offset);
        
        ExecBatch batch = snapshot.makeBatch(offset, currentBatchSize);
        batch.userData = const_cast<void*>(static_cast<const void*>(&ctx));
        batch.batchIndex = offset / batchSize;
        
        // For read-only kernels, input and output can be the same
        result = kernel->exec(&batch, &batch);
    }

    return result;
}

Status KernelRegistry::executeAll(KernelMode mode, World& world, const KernelContext& ctx) {
    auto kernels = getKernelsByMode(mode);
    
    // Sort by priority
    std::sort(kernels.begin(), kernels.end(),
              [](const Kernel* a, const Kernel* b) {
                  return a->priority < b->priority;
              });

    Status result = Status::Ok;
    for (const Kernel* kernel : kernels) {
        if (!isOk(result)) break;
        result = execute(kernel->name, world, ctx);
    }

    return result;
}

// ============================================================================
// Configuration
// ============================================================================

void KernelRegistry::setDefaultBatchSize(size_t size) {
    defaultBatchSize_ = size > 0 ? size : DEFAULT_BATCH_SIZE;
}

} // namespace jupiter::ecs

