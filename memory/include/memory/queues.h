#pragma once

#include <atomic>
#include <cstdint>
#include <new>  // for placement new

/**
 * @file queues.h
 * @brief Lock-free queue implementations using ring buffers and atomics
 *
 * Following CLAUDE.md principles:
 * - Lock-free design using std::atomic
 * - Pre-allocated ring buffers (no runtime allocations)
 * - Template-based to work with any type
 * - Cache-friendly design with proper alignment
 */

namespace jupiter {
namespace memory {

/**
 * @brief Single-Producer Single-Consumer lock-free queue
 *
 * Optimized for the common case of one producer and one consumer.
 * Uses relaxed memory ordering where possible for maximum performance.
 *
 * @tparam T Type of elements in the queue
 * @tparam Capacity Maximum number of elements (must be power of 2)
 *
 * Example:
 * @code
 * SPSCQueue<int, 256> queue;
 * queue.push(42);
 * int value;
 * if (queue.pop(value)) {
 *     // value == 42
 * }
 * @endcode
 */
template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    SPSCQueue()
        : head_(0)
        , tail_(0) {
        static_assert(sizeof(T) > 0, "T must be a complete type");
    }

    ~SPSCQueue() {
        // Destroy any remaining elements
        T item;
        while (pop(item)) {
            // Destructor called automatically
        }
    }

    // No copy or move (queue contains pre-allocated buffer)
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    /**
     * @brief Push an element onto the queue (producer side)
     * @param item Element to push
     * @return true if successful, false if queue is full
     */
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & kIndexMask;

        // Check if queue is full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue is full
        }

        // Construct element in-place using placement new
        new (&buffer_[current_tail]) T(item);

        // Publish the new tail (release semantics ensure writes are visible to consumer)
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Push an element onto the queue (move version)
     * @param item Element to push (will be moved)
     * @return true if successful, false if queue is full
     */
    bool push(T&& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & kIndexMask;

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        new (&buffer_[current_tail]) T(std::move(item));
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an element from the queue (consumer side)
     * @param item Reference to store the popped element
     * @return true if successful, false if queue is empty
     */
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Check if queue is empty
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;  // Queue is empty
        }

        // Move element out
        item = std::move(buffer_[current_head]);

        // Explicitly call destructor (we used placement new)
        buffer_[current_head].~T();

        // Publish the new head
        head_.store((current_head + 1) & kIndexMask, std::memory_order_release);
        return true;
    }

    /**
     * @brief Check if queue is empty
     * @return true if empty
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get approximate size (may be stale)
     * @return Number of elements in queue
     */
    size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h) & kIndexMask;
    }

    /**
     * @brief Get queue capacity
     * @return Maximum number of elements
     */
    constexpr size_t capacity() const { return Capacity; }

private:
    static constexpr size_t kIndexMask = Capacity - 1;

    // Cache-line padding to avoid false sharing
    alignas(64) std::atomic<size_t> head_;  // Consumer's index
    alignas(64) std::atomic<size_t> tail_;  // Producer's index

    // Ring buffer storage (uninitialized until elements are pushed)
    alignas(alignof(T)) uint8_t storage_[Capacity * sizeof(T)];

    // Typed view of storage
    T* buffer_ = reinterpret_cast<T*>(storage_);
};

/**
 * @brief Multi-Producer Multi-Consumer lock-free queue
 *
 * Supports multiple producers and multiple consumers.
 * Uses compare-and-swap (CAS) operations for thread-safety.
 * More expensive than SPSC but supports concurrent access.
 *
 * @tparam T Type of elements in the queue
 * @tparam Capacity Maximum number of elements (must be power of 2)
 *
 * Example:
 * @code
 * MPMCQueue<int, 256> queue;
 * // Thread 1:
 * queue.push(42);
 * // Thread 2:
 * int value;
 * if (queue.pop(value)) {
 *     // value == 42
 * }
 * @endcode
 */
template <typename T, size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    struct Slot {
        std::atomic<size_t> sequence;
        alignas(alignof(T)) uint8_t storage[sizeof(T)];

        T* ptr() { return reinterpret_cast<T*>(storage); }
    };

public:
    MPMCQueue()
        : head_(0)
        , tail_(0) {
        static_assert(sizeof(T) > 0, "T must be a complete type");

        // Initialize sequence numbers
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MPMCQueue() {
        // Destroy any remaining elements
        T item;
        while (pop(item)) {
            // Destructor called automatically
        }
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    MPMCQueue(MPMCQueue&&) = delete;
    MPMCQueue& operator=(MPMCQueue&&) = delete;

    /**
     * @brief Push an element onto the queue (thread-safe)
     * @param item Element to push
     * @return true if successful, false if queue is full
     */
    bool push(const T& item) {
        Slot* slot;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            slot = &buffer_[pos & kIndexMask];
            size_t seq = slot->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is available, try to claim it
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // Successfully claimed slot
                }
            } else if (diff < 0) {
                return false;  // Queue is full
            } else {
                // Another thread is working on this slot, reload position
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        // Construct element in claimed slot
        new (slot->ptr()) T(item);

        // Publish element
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Push an element onto the queue (move version, thread-safe)
     * @param item Element to push (will be moved)
     * @return true if successful, false if queue is full
     */
    bool push(T&& item) {
        Slot* slot;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            slot = &buffer_[pos & kIndexMask];
            size_t seq = slot->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        new (slot->ptr()) T(std::move(item));
        slot->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an element from the queue (thread-safe)
     * @param item Reference to store the popped element
     * @return true if successful, false if queue is empty
     */
    bool pop(T& item) {
        Slot* slot;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            slot = &buffer_[pos & kIndexMask];
            size_t seq = slot->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                // Element is ready, try to claim it
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;  // Successfully claimed element
                }
            } else if (diff < 0) {
                return false;  // Queue is empty
            } else {
                // Another thread is working on this slot, reload position
                pos = head_.load(std::memory_order_relaxed);
            }
        }

        // Move element out
        item = std::move(*slot->ptr());

        // Destroy element
        slot->ptr()->~T();

        // Mark slot as empty
        slot->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    /**
     * @brief Check if queue is empty (may be stale in concurrent context)
     * @return true if empty
     */
    bool empty() const {
        size_t pos = head_.load(std::memory_order_acquire);
        const Slot* slot = &buffer_[pos & kIndexMask];
        size_t seq = slot->sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1) < 0;
    }

    /**
     * @brief Get queue capacity
     * @return Maximum number of elements
     */
    constexpr size_t capacity() const { return Capacity; }

private:
    static constexpr size_t kIndexMask = Capacity - 1;

    // Cache-line padding to avoid false sharing
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;

    // Ring buffer of slots
    alignas(64) Slot buffer_[Capacity];
};

} // namespace memory
} // namespace jupiter
