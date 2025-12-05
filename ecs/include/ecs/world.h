#pragma once

/**
 * @file world.h
 * @brief Double-buffered ECS World with multi-reader support
 * 
 * The World is the central container for all entity data. It uses a
 * double-buffered (tick-tock) design that enables:
 * - N stable reader threads (render, audio, network, AI)
 * - 1 writer thread (game/physics tick)
 * - Lock-free read access
 * - Clone-on-write for efficient updates
 */

#include "types.h"
#include "span.h"
#include "exec_batch.h"
#include "entity_buffer.h"
#include "read_snapshot.h"
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>

namespace jupiter::ecs {

/**
 * @brief Entity creation parameters
 */
struct EntityCreateInfo {
    EntityId id = INVALID_ENTITY;       // 0 for auto-assign
    EntityFlags flags = EntityFlags::Active;
    
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    // Physics (optional)
    glm::vec3 linearVelocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    float mass = 1.0f;  // 0 = static
    
    // Rendering (optional)
    uint32_t materialId = 0;
    uint32_t meshId = 0;
};

/**
 * @brief Deferred operation types
 */
enum class DeferredOpType : uint8_t {
    Create,
    Destroy
};

/**
 * @brief Deferred operation for end-of-frame processing
 */
struct DeferredOp {
    DeferredOpType type;
    union {
        EntityCreateInfo createInfo;
        uint32_t destroyIndex;
    };
    
    DeferredOp() : type(DeferredOpType::Create) {}
};

/**
 * @brief Double-buffered ECS World
 * 
 * Thread Model:
 * - Reader threads call acquireReadSnapshot() to get stable data
 * - Writer thread modifies writeBuffer() and calls markDirty()
 * - swap() atomically switches buffers (invalidates old snapshots)
 * 
 * Frame Loop:
 * 1. Readers: snapshot = world.acquireReadSnapshot()
 * 2. Writer: world.executeWriteKernel(physics), world.markDirty(...)
 * 3. Barrier: all readers done with snapshot
 * 4. Writer: world.swap()
 * 5. Repeat
 */
class World {
public:
    /**
     * @brief Construct world with initial capacity
     */
    explicit World(uint32_t capacity);

    ~World() = default;

    // Non-copyable and non-movable (due to std::atomic members)
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    // ========================================================================
    // Reader API (thread-safe, lock-free)
    // ========================================================================

    /**
     * @brief Acquire a stable read snapshot
     * 
     * Thread-safe. Multiple readers can acquire snapshots concurrently.
     * Snapshot is valid until next swap().
     */
    [[nodiscard]] ReadSnapshot acquireReadSnapshot() const noexcept;

    /**
     * @brief Get current generation (incremented on swap)
     */
    [[nodiscard]] uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get entity count (from read buffer)
     */
    [[nodiscard]] size_t entityCount() const noexcept {
        return buffers_[readIdx_.load(std::memory_order_acquire)].count;
    }

    // ========================================================================
    // Writer API (single-threaded)
    // ========================================================================

    /**
     * @brief Get write buffer for mutation
     * 
     * NOT thread-safe. Only writer thread should call this.
     */
    [[nodiscard]] EntityBuffer& writeBuffer() noexcept {
        return buffers_[readIdx_.load(std::memory_order_acquire) ^ 1];
    }

    /**
     * @brief Mark column(s) as dirty
     * 
     * Triggers clone-on-write if generations differ.
     */
    void markDirty(ColumnId cols) noexcept;

    /**
     * @brief Check if column is dirty this frame
     */
    [[nodiscard]] bool isDirty(ColumnId col) const noexcept {
        return hasColumn(static_cast<ColumnId>(dirtyMask_.load(std::memory_order_acquire)), col);
    }

    /**
     * @brief Get dirty mask
     */
    [[nodiscard]] ColumnId dirtyMask() const noexcept {
        return static_cast<ColumnId>(dirtyMask_.load(std::memory_order_acquire));
    }

    /**
     * @brief Swap buffers (invalidates all snapshots)
     * 
     * Call once per frame after all writes complete.
     * After swap, readers see the newly written data.
     */
    void swap() noexcept;

    // ========================================================================
    // Entity Management
    // ========================================================================

    /**
     * @brief Create entity immediately (writer thread only)
     * @return Index of created entity, or UINT32_MAX on failure
     */
    uint32_t create(const EntityCreateInfo& info);

    /**
     * @brief Destroy entity immediately (writer thread only)
     */
    void destroy(uint32_t index);

    /**
     * @brief Queue entity creation for end-of-frame processing
     * 
     * Thread-safe. Can be called from any thread.
     */
    void queueCreate(const EntityCreateInfo& info);

    /**
     * @brief Queue entity destruction for end-of-frame processing
     * 
     * Thread-safe. Can be called from any thread.
     */
    void queueDestroy(uint32_t index);

    /**
     * @brief Process all queued operations
     * 
     * Call at end of frame, before swap().
     */
    void processQueued();

    // ========================================================================
    // Batch Creation (Kernel Interface)
    // ========================================================================

    /**
     * @brief Create an ExecBatch for reading (from read buffer)
     */
    [[nodiscard]] ExecBatch makeReadBatch(size_t offset, size_t length) const noexcept;

    /**
     * @brief Create an ExecBatch for writing (to write buffer)
     */
    [[nodiscard]] ExecBatch makeWriteBatch(size_t offset, size_t length) noexcept;

    /**
     * @brief Create batch spanning all entities
     */
    [[nodiscard]] ExecBatch makeBatch(size_t offset, size_t length) const noexcept {
        return makeReadBatch(offset, length);
    }

    // ========================================================================
    // Capacity
    // ========================================================================

    [[nodiscard]] uint32_t capacity() const noexcept { 
        return buffers_[0].capacity; 
    }

    /**
     * @brief Grow capacity if needed
     */
    void reserve(uint32_t newCapacity);

private:
    // Double buffers (tick-tock)
    EntityBuffer buffers_[2];
    
    // Atomic state for thread-safe access
    std::atomic<uint64_t> generation_{0};
    std::atomic<uint32_t> dirtyMask_{0};
    std::atomic<uint8_t> readIdx_{0};
    
    // Free list for entity index reuse
    std::vector<uint32_t> freeIndices_;
    
    // Deferred operations queue
    std::mutex queueMutex_;
    std::queue<DeferredOp> opQueue_;
    
    // Next entity ID (for auto-assignment)
    uint32_t nextEntityId_ = 1;

    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /**
     * @brief Clone column from read to write buffer (for clone-on-write)
     */
    void cloneColumn(ColumnId col);

    /**
     * @brief Allocate entity index from free list or extend
     */
    uint32_t allocateIndex();

    /**
     * @brief Return index to free list
     */
    void freeIndex(uint32_t index);

    /**
     * @brief Create entity at specific index in both buffers
     */
    void createAt(uint32_t index, const EntityCreateInfo& info);
};

} // namespace jupiter::ecs

