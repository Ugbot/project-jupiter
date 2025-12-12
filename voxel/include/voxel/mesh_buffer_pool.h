#pragma once

#include "voxel_types.h"
#include <atomic>
#include <cstdint>

/**
 * @file mesh_buffer_pool.h
 * @brief Pre-allocated pool of mesh generation buffers
 *
 * Following Project Jupiter principles:
 * - All memory pre-allocated at initialization
 * - Lock-free buffer acquisition/release
 * - No runtime allocations during meshing
 */

namespace jupiter {
namespace voxel {

/**
 * @brief A single mesh buffer for stb_voxel_render output
 *
 * Contains the raw vertex buffer and metadata.
 * Size is fixed at compile time for predictable memory usage.
 */
struct MeshBuffer {
    /// Maximum quads per mesh
    static constexpr uint32_t MAX_QUADS = 8192;

    /// Maximum vertices (4 per quad)
    static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;

    /// Bytes per stb vertex (Mode 30 = 8 bytes)
    static constexpr uint32_t STB_VERTEX_SIZE = 8;

    /// Total buffer size for stb_voxel_render output
    static constexpr size_t BUFFER_SIZE = MAX_VERTICES * STB_VERTEX_SIZE;  // 256KB

    /// The actual vertex data buffer
    alignas(64) uint8_t data[BUFFER_SIZE];

    /// Pool index for return tracking
    uint32_t poolIndex = 0;

    /// In-use flag for debugging
    std::atomic<bool> inUse{false};
};

static_assert(sizeof(MeshBuffer) >= MeshBuffer::BUFFER_SIZE, "MeshBuffer data array too small");

/**
 * @brief Lock-free pool of mesh buffers
 *
 * Pre-allocates a fixed number of MeshBuffer instances.
 * Uses simple atomic index-based free list for lock-free acquire/release.
 */
class MeshBufferPool {
public:
    /// Maximum number of buffers in pool
    static constexpr uint32_t MAX_BUFFERS = 16;

    /// Default number of buffers (enough for parallel meshing threads)
    static constexpr uint32_t DEFAULT_BUFFER_COUNT = 8;

    MeshBufferPool() = default;
    ~MeshBufferPool();

    // Non-copyable, non-movable (owns large memory block)
    MeshBufferPool(const MeshBufferPool&) = delete;
    MeshBufferPool& operator=(const MeshBufferPool&) = delete;
    MeshBufferPool(MeshBufferPool&&) = delete;
    MeshBufferPool& operator=(MeshBufferPool&&) = delete;

    /**
     * @brief Initialize the pool with pre-allocated buffers
     *
     * @param bufferCount Number of buffers to allocate (max MAX_BUFFERS)
     * @return true if initialization successful
     */
    bool initialize(uint32_t bufferCount = DEFAULT_BUFFER_COUNT);

    /**
     * @brief Shutdown and free all buffers
     */
    void shutdown();

    /**
     * @brief Acquire a buffer from the pool (lock-free)
     *
     * @return Pointer to buffer, or nullptr if none available
     */
    MeshBuffer* acquire();

    /**
     * @brief Release a buffer back to the pool (lock-free)
     *
     * @param buffer Buffer to release (must have been acquired from this pool)
     */
    void release(MeshBuffer* buffer);

    /**
     * @brief Get number of buffers currently available
     */
    uint32_t getAvailableCount() const;

    /**
     * @brief Get total number of buffers in pool
     */
    uint32_t getTotalCount() const { return bufferCount_; }

    /**
     * @brief Check if pool is initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    /// Pre-allocated buffer storage
    MeshBuffer* buffers_ = nullptr;

    /// Free list (stack-based, atomic)
    /// freeStack_[i] = index of next free buffer (UINT32_MAX = end)
    std::atomic<uint32_t> freeStackTop_{UINT32_MAX};
    uint32_t freeStack_[MAX_BUFFERS];

    /// Number of buffers
    uint32_t bufferCount_ = 0;

    /// Initialization state
    bool initialized_ = false;
};

/**
 * @brief RAII wrapper for acquiring/releasing mesh buffers
 *
 * Automatically returns buffer to pool on destruction.
 */
class ScopedMeshBuffer {
public:
    ScopedMeshBuffer(MeshBufferPool& pool)
        : pool_(pool)
        , buffer_(pool.acquire())
    {}

    ~ScopedMeshBuffer() {
        if (buffer_) {
            pool_.release(buffer_);
        }
    }

    // Non-copyable
    ScopedMeshBuffer(const ScopedMeshBuffer&) = delete;
    ScopedMeshBuffer& operator=(const ScopedMeshBuffer&) = delete;

    // Movable
    ScopedMeshBuffer(ScopedMeshBuffer&& other) noexcept
        : pool_(other.pool_)
        , buffer_(other.buffer_)
    {
        other.buffer_ = nullptr;
    }

    ScopedMeshBuffer& operator=(ScopedMeshBuffer&&) = delete;

    /// Get the buffer (may be nullptr if pool exhausted)
    MeshBuffer* get() { return buffer_; }
    const MeshBuffer* get() const { return buffer_; }

    /// Check if buffer was acquired
    bool valid() const { return buffer_ != nullptr; }

    /// Access buffer data
    uint8_t* data() { return buffer_ ? buffer_->data : nullptr; }
    const uint8_t* data() const { return buffer_ ? buffer_->data : nullptr; }

private:
    MeshBufferPool& pool_;
    MeshBuffer* buffer_;
};

} // namespace voxel
} // namespace jupiter
