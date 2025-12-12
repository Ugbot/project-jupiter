/**
 * @file mesh_buffer_pool.cpp
 * @brief Implementation of MeshBufferPool
 */

#include <voxel/mesh_buffer_pool.h>
#include <cstring>
#include <algorithm>

namespace jupiter {
namespace voxel {

MeshBufferPool::~MeshBufferPool() {
    shutdown();
}

bool MeshBufferPool::initialize(uint32_t bufferCount) {
    if (initialized_) {
        return true;  // Already initialized
    }

    if (bufferCount == 0 || bufferCount > MAX_BUFFERS) {
        return false;
    }

    // Allocate buffer array (single large allocation)
    buffers_ = new MeshBuffer[bufferCount];
    if (!buffers_) {
        return false;
    }

    // Initialize buffer metadata
    for (uint32_t i = 0; i < bufferCount; ++i) {
        buffers_[i].poolIndex = i;
        buffers_[i].inUse.store(false, std::memory_order_relaxed);
        std::memset(buffers_[i].data, 0, MeshBuffer::BUFFER_SIZE);
    }

    // Initialize free stack (all buffers are free initially)
    // Stack grows down: freeStack_[0] is top
    for (uint32_t i = 0; i < bufferCount; ++i) {
        freeStack_[i] = i;
    }
    freeStackTop_.store(bufferCount - 1, std::memory_order_release);

    bufferCount_ = bufferCount;
    initialized_ = true;

    return true;
}

void MeshBufferPool::shutdown() {
    if (!initialized_) {
        return;
    }

    delete[] buffers_;
    buffers_ = nullptr;

    freeStackTop_.store(UINT32_MAX, std::memory_order_relaxed);
    bufferCount_ = 0;
    initialized_ = false;
}

MeshBuffer* MeshBufferPool::acquire() {
    if (!initialized_) {
        return nullptr;
    }

    // Try to pop from free stack
    uint32_t top = freeStackTop_.load(std::memory_order_acquire);

    while (top != UINT32_MAX) {
        // Try to decrement top atomically
        uint32_t newTop = (top == 0) ? UINT32_MAX : top - 1;

        if (freeStackTop_.compare_exchange_weak(top, newTop,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
            // Successfully popped
            uint32_t index = freeStack_[top];

            MeshBuffer* buffer = &buffers_[index];

            // Mark as in use
            bool expected = false;
            if (!buffer->inUse.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
                // Already in use - shouldn't happen, but handle it
                continue;
            }

            return buffer;
        }
        // CAS failed, top was updated, retry with new value
    }

    return nullptr;  // No buffers available
}

void MeshBufferPool::release(MeshBuffer* buffer) {
    if (!initialized_ || !buffer) {
        return;
    }

    // Verify buffer belongs to this pool
    if (buffer < buffers_ || buffer >= buffers_ + bufferCount_) {
        return;  // Not our buffer
    }

    uint32_t index = buffer->poolIndex;

    // Mark as not in use
    buffer->inUse.store(false, std::memory_order_release);

    // Push back to free stack
    uint32_t top = freeStackTop_.load(std::memory_order_acquire);

    while (true) {
        uint32_t newTop = (top == UINT32_MAX) ? 0 : top + 1;

        if (newTop >= bufferCount_) {
            // Stack full (shouldn't happen if usage is correct)
            return;
        }

        freeStack_[newTop] = index;

        if (freeStackTop_.compare_exchange_weak(top, newTop,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
            return;  // Successfully pushed
        }
        // CAS failed, retry with new top
    }
}

uint32_t MeshBufferPool::getAvailableCount() const {
    if (!initialized_) {
        return 0;
    }

    uint32_t top = freeStackTop_.load(std::memory_order_acquire);
    if (top == UINT32_MAX) {
        return 0;
    }
    return top + 1;
}

} // namespace voxel
} // namespace jupiter
